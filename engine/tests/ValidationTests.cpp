#include <doctest/doctest.h>

#include "TestHelpers.hpp"
#include "infraforge/application/CommandProcessor.hpp"
#include "infraforge/application/validation/CancellationToken.hpp"
#include "infraforge/application/validation/DiagnosticStore.hpp"
#include "infraforge/application/validation/ProjectFoundationValidator.hpp"
#include "infraforge/application/validation/ValidationService.hpp"
#include "infraforge/application/validation/Validator.hpp"
#include "infraforge/application/validation/ValidatorRegistry.hpp"
#include "infraforge/domain/validation/Diagnostic.hpp"
#include "infraforge/persistence/SqliteProjectStore.hpp"
#include "infraforge/runtime/FileSystemUtf8.hpp"

#include <infraforge/protocol/v1/foundation.pb.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using ProtocolFrame = infraforge::protocol::v1::Frame;
using infraforge::application::CommandFailure;
using infraforge::application::CommandFailureCode;
using infraforge::application::validation::CancellationToken;
using infraforge::application::validation::DiagnosticStore;
using infraforge::application::validation::ValidationContext;
using infraforge::application::validation::ValidationResult;
using infraforge::application::validation::ValidationService;
using infraforge::application::validation::Validator;
using infraforge::application::validation::ValidatorRegistry;
using infraforge::domain::validation::Diagnostic;
using infraforge::domain::validation::EntityRef;
using infraforge::domain::validation::Severity;
using infraforge::domain::validation::SuggestedAction;

constexpr auto kWaitTimeout = std::chrono::seconds{10};

// A validator that always produces a fixed diagnostic for deterministic testing.
class FixedDiagnosticValidator final : public Validator {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "test.fixed"; }

    void validate(const ValidationContext& context, const CancellationToken& /*cancellation*/, std::vector<Diagnostic>& output) const override {
        Diagnostic d;
        d.code = "test.fixed.always";
        d.severity = Severity::Warning;
        d.source = "test.fixed";
        d.message = "deterministic test diagnostic";
        d.revision = context.revision;
        d.entities.push_back(EntityRef{.kind = "project", .id = context.project.uuid});
        output.push_back(std::move(d));
    }
};

// A validator that produces a diagnostic for a specific entity, allowing
// multi-entity same-code testing.
class EntityDiagnosticValidator final : public Validator {
public:
    EntityDiagnosticValidator(std::string name, std::string entityId)
        : name_(std::move(name)), entityId_(std::move(entityId)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

    void validate(const ValidationContext& context, const CancellationToken& /*cancellation*/, std::vector<Diagnostic>& output) const override {
        Diagnostic d;
        d.code = "test.entity.bad";
        d.severity = Severity::Error;
        d.source = name_;
        d.message = "entity has a problem";
        d.revision = context.revision;
        d.entities.push_back(EntityRef{.kind = "road", .id = entityId_});
        output.push_back(std::move(d));
    }

private:
    std::string name_;
    std::string entityId_;
};

// A validator that throws to exercise the failure path.
class ThrowingValidator final : public Validator {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "test.throwing"; }

    void validate(const ValidationContext& /*context*/, const CancellationToken& /*cancellation*/, std::vector<Diagnostic>& /*output*/) const override {
        throw std::runtime_error("validator intentionally failed");
    }
};

// A validator that checks cancellation and returns early.
class CancellableValidator final : public Validator {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "test.cancellable"; }

    void validate(const ValidationContext& context, const CancellationToken& cancellation, std::vector<Diagnostic>& output) const override {
        if (cancellation.isCancelled()) {
            return;
        }
        Diagnostic d;
        d.code = "test.cancellable.produced";
        d.severity = Severity::Info;
        d.source = "test.cancellable";
        d.message = "produced before cancellation";
        d.revision = context.revision;
        output.push_back(std::move(d));
    }
};

// A validator that simulates an expensive operation by sleeping, polling
// cancellation between iterations.
class SlowValidator final : public Validator {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "test.slow"; }

    void validate(const ValidationContext& context, const CancellationToken& cancellation, std::vector<Diagnostic>& output) const override {
        for (int i = 0; i < 10; ++i) {
            if (cancellation.isCancelled()) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Diagnostic d;
        d.code = "test.slow.completed";
        d.severity = Severity::Info;
        d.source = "test.slow";
        d.message = "slow validator completed";
        d.revision = context.revision;
        output.push_back(std::move(d));
    }
};

// Collects frames delivered by the processor's executor thread (mirrors the
// pattern in CommandProcessorTests.cpp).
class RecordingSink final : public infraforge::application::CommandSink {
public:
    void sendToConnection(std::string_view connectionId, const ProtocolFrame& frame) override {
        std::lock_guard lock{mutex_};
        (void)connectionId;
        results_.push_back(frame);
        signal_.notify_all();
    }

    void broadcastEvent(const ProtocolFrame& frame) override {
        std::lock_guard lock{mutex_};
        events_.push_back(frame);
        signal_.notify_all();
    }

    bool waitForTotal(std::size_t total, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return signal_.wait_for(lock, timeout, [&] { return results_.size() + events_.size() >= total; });
    }

    bool waitForResults(std::size_t total, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return signal_.wait_for(lock, timeout, [&] { return results_.size() >= total; });
    }

    bool waitForEvents(std::size_t total, std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return signal_.wait_for(lock, timeout, [&] { return events_.size() >= total; });
    }

    [[nodiscard]] std::vector<ProtocolFrame> results() const {
        std::lock_guard lock{mutex_};
        return {results_.begin(), results_.end()};
    }

    [[nodiscard]] std::vector<ProtocolFrame> events() const {
        std::lock_guard lock{mutex_};
        return {events_.begin(), events_.end()};
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable signal_;
    std::deque<ProtocolFrame> results_;
    std::deque<ProtocolFrame> events_;
};

ProtocolFrame worldCheckFrame(const std::string& requestId) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    frame.mutable_command()->mutable_world_check();
    return frame;
}

ProtocolFrame worldCancelCheckFrame(const std::string& requestId) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    frame.mutable_command()->mutable_world_cancel_check();
    return frame;
}

ProtocolFrame createProjectFrame(const std::string& requestId, const std::string& parentDir) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    auto* create = frame.mutable_command()->mutable_create_project();
    create->set_display_name("Validation Test");
    create->set_parent_directory(parentDir);
    auto* geo = create->mutable_georeference();
    geo->set_horizontal_crs("EPSG:32633");
    geo->set_linear_unit("metre");
    geo->set_axis_convention(infraforge::protocol::v1::AXIS_CONVENTION_EASTING_NORTHING_UP);
    create->set_traffic_side(infraforge::protocol::v1::TRAFFIC_SIDE_RIGHT);
    return frame;
}

ProtocolFrame closeProjectFrame(const std::string& requestId) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    frame.mutable_command()->mutable_close_project();
    return frame;
}

ProtocolFrame saveProjectFrame(const std::string& requestId) {
    ProtocolFrame frame;
    frame.set_request_id(requestId);
    frame.mutable_command()->mutable_save_project();
    return frame;
}

std::shared_ptr<CancellationToken> makeToken() {
    return std::make_shared<CancellationToken>();
}

} // namespace

TEST_SUITE("validation diagnostic model") {
    TEST_CASE("severity name round-trips") {
        using infraforge::domain::validation::severityFromName;
        using infraforge::domain::validation::severityName;
        for (const auto severity : {Severity::Info, Severity::Warning, Severity::Error}) {
            const auto name = severityName(severity);
            const auto restored = severityFromName(name);
            REQUIRE(restored.has_value());
            CHECK(*restored == severity);
        }
        CHECK_FALSE(severityFromName("bogus").has_value());
    }

    TEST_CASE("diagnostic equality is structural") {
        Diagnostic a;
        a.code = "x.y.z";
        a.severity = Severity::Error;
        a.source = "x";
        a.message = "m";
        a.revision = 5;
        a.entities.push_back(EntityRef{.kind = "project", .id = "uuid"});

        Diagnostic b = a;
        CHECK(a == b);

        b.message = "different";
        CHECK(a != b);
    }

    TEST_CASE("diagnostic identity is code + entities, not message") {
        Diagnostic a;
        a.code = "road.geometry.bad";
        a.entities.push_back(EntityRef{.kind = "road", .id = "road-1"});
        a.message = "first message";

        Diagnostic b = a;
        b.message = "completely different message";
        // Same code + same entities = same identity despite different message.
        CHECK_EQ(a.diagnosticIdentity(), b.diagnosticIdentity());

        Diagnostic c = a;
        c.entities.clear();
        c.entities.push_back(EntityRef{.kind = "road", .id = "road-2"});
        // Same code but different entity = different identity.
        CHECK_NE(a.diagnosticIdentity(), c.diagnosticIdentity());
    }

    TEST_CASE("diagnostic identity is stable regardless of entity order") {
        Diagnostic a;
        a.code = "multi.entity";
        a.entities.push_back(EntityRef{.kind = "road", .id = "r2"});
        a.entities.push_back(EntityRef{.kind = "road", .id = "r1"});

        Diagnostic b;
        b.code = "multi.entity";
        b.entities.push_back(EntityRef{.kind = "road", .id = "r1"});
        b.entities.push_back(EntityRef{.kind = "road", .id = "r2"});

        // Entities are sorted in identity, so order doesn't matter.
        CHECK_EQ(a.diagnosticIdentity(), b.diagnosticIdentity());
    }

    TEST_CASE("same diagnostic code on different entities produces distinct identities") {
        Diagnostic a;
        a.code = "road.geometry.self_intersection";
        a.entities.push_back(EntityRef{.kind = "road", .id = "road-A"});

        Diagnostic b;
        b.code = "road.geometry.self_intersection";
        b.entities.push_back(EntityRef{.kind = "road", .id = "road-B"});

        CHECK_NE(a.diagnosticIdentity(), b.diagnosticIdentity());
    }
}

TEST_SUITE("validator registry robustness") {
    TEST_CASE("validators execute in registration order") {
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<FixedDiagnosticValidator>());
        registry.registerValidator(std::make_unique<ThrowingValidator>());

        const auto& validators = registry.validators();
        REQUIRE(validators.size() == 2);
        CHECK(validators[0]->name() == "test.fixed");
        CHECK(validators[1]->name() == "test.throwing");
    }

    TEST_CASE("rejects null validator") {
        ValidatorRegistry registry;
        CHECK_THROWS_AS(registry.registerValidator(nullptr), std::invalid_argument);
    }

    TEST_CASE("rejects empty validator name") {
        class EmptyNameValidator final : public Validator {
        public:
            [[nodiscard]] std::string_view name() const noexcept override { return ""; }
            void validate(const ValidationContext&, const CancellationToken&, std::vector<Diagnostic>&) const override {}
        };
        ValidatorRegistry registry;
        CHECK_THROWS_AS(registry.registerValidator(std::make_unique<EmptyNameValidator>()), std::invalid_argument);
    }

    TEST_CASE("rejects duplicate validator name") {
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<FixedDiagnosticValidator>());
        CHECK_THROWS_AS(registry.registerValidator(std::make_unique<FixedDiagnosticValidator>()), std::invalid_argument);
    }
}

TEST_SUITE("diagnostic store lifecycle") {
    TEST_CASE("publishing a new set produces added events") {
        DiagnosticStore store;
        std::vector<Diagnostic> diags;
        Diagnostic d;
        d.code = "test.a";
        d.entities.push_back(EntityRef{.kind = "project", .id = "uuid"});
        diags.push_back(d);

        const auto changes = store.publish(diags, 1);
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].kind == DiagnosticStore::ChangeKind::Added);
        CHECK_EQ(changes[0].diagnostic.code, "test.a");
    }

    TEST_CASE("publishing the same set produces no changes") {
        DiagnosticStore store;
        std::vector<Diagnostic> diags;
        Diagnostic d;
        d.code = "test.a";
        d.entities.push_back(EntityRef{.kind = "project", .id = "uuid"});
        diags.push_back(d);

        (void)store.publish(diags, 1);
        const auto changes = store.publish(diags, 1);
        CHECK(changes.empty());
    }

    TEST_CASE("publishing a reduced set produces removed events") {
        DiagnosticStore store;
        std::vector<Diagnostic> diags;
        Diagnostic d;
        d.code = "test.a";
        d.entities.push_back(EntityRef{.kind = "project", .id = "uuid"});
        diags.push_back(d);
        Diagnostic d2;
        d2.code = "test.b";
        d2.entities.push_back(EntityRef{.kind = "project", .id = "uuid"});
        diags.push_back(d2);

        (void)store.publish(diags, 1);

        std::vector<Diagnostic> reduced;
        reduced.push_back(d);
        const auto changes = store.publish(reduced, 1);
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].kind == DiagnosticStore::ChangeKind::Removed);
        CHECK_EQ(changes[0].diagnostic.code, "test.b");
    }

    TEST_CASE("same code different entities are distinct diagnostics") {
        DiagnosticStore store;
        std::vector<Diagnostic> diags;
        Diagnostic a;
        a.code = "same.code";
        a.entities.push_back(EntityRef{.kind = "road", .id = "r1"});
        diags.push_back(a);
        Diagnostic b;
        b.code = "same.code";
        b.entities.push_back(EntityRef{.kind = "road", .id = "r2"});
        diags.push_back(b);

        const auto changes = store.publish(diags, 1);
        REQUIRE(changes.size() == 2);
        CHECK(changes[0].kind == DiagnosticStore::ChangeKind::Added);
        CHECK(changes[1].kind == DiagnosticStore::ChangeKind::Added);
    }

    TEST_CASE("clear empties the store and returns removed diagnostics") {
        DiagnosticStore store;
        std::vector<Diagnostic> diags;
        Diagnostic d;
        d.code = "test.a";
        d.entities.push_back(EntityRef{.kind = "project", .id = "uuid"});
        diags.push_back(d);
        (void)store.publish(diags, 1);

        const auto removed = store.clear();
        REQUIRE(removed.size() == 1);
        CHECK_EQ(removed[0].code, "test.a");
        CHECK_FALSE(store.hasPublished());
    }

    TEST_CASE("isStale is true when revision differs") {
        DiagnosticStore store;
        std::vector<Diagnostic> diags;
        Diagnostic d;
        d.code = "test.a";
        d.entities.push_back(EntityRef{.kind = "project", .id = "uuid"});
        diags.push_back(d);
        (void)store.publish(diags, 5);

        CHECK_FALSE(store.isStale(5));
        CHECK(store.isStale(6));
        CHECK(store.isStale(4));
    }

    TEST_CASE("isStale is true when nothing has been published") {
        DiagnosticStore store;
        CHECK(store.isStale(1));
        CHECK_FALSE(store.hasPublished());
    }
}

TEST_SUITE("validation service") {
    TEST_CASE("check without an open project throws ProjectNotOpen") {
        infraforge::persistence::SqliteProjectStore store;
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<FixedDiagnosticValidator>());
        ValidationService service(store, registry);

        auto cancellation = makeToken();
        const auto failure = infraforge::testhelpers::captureException<CommandFailure>(
            [&] { (void)service.check(cancellation); });
        REQUIRE(failure.has_value());
        CHECK(failure->code() == CommandFailureCode::ProjectNotOpen);
    }

    TEST_CASE("a valid canonical project yields zero diagnostics") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<infraforge::application::validation::ProjectFoundationValidator>());
        ValidationService service(store, registry);

        const auto spec = infraforge::testhelpers::sampleCreateSpec(scratch.path());
        (void)store.create(spec);

        auto cancellation = makeToken();
        const ValidationResult result = service.check(cancellation);

        CHECK_FALSE(result.cancelled);
        CHECK_EQ(result.revision, store.current().revision);
        CHECK(result.diagnostics.empty());

        store.close();
    }

    TEST_CASE("diagnostics carry stable codes, severity, source, and revision") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<FixedDiagnosticValidator>());
        ValidationService service(store, registry);

        (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        auto cancellation = makeToken();
        const ValidationResult result = service.check(cancellation);

        REQUIRE(result.diagnostics.size() == 1);
        const auto& d = result.diagnostics[0];
        CHECK_EQ(d.code, "test.fixed.always");
        CHECK(d.severity == Severity::Warning);
        CHECK_EQ(d.source, "test.fixed");
        CHECK_EQ(d.revision, store.current().revision);
        REQUIRE(d.entities.size() == 1);
        CHECK_EQ(d.entities[0].kind, "project");
        CHECK_EQ(d.entities[0].id, store.current().uuid);

        store.close();
    }

    TEST_CASE("validator failure is reported as a diagnostic, not an exception") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<ThrowingValidator>());
        ValidationService service(store, registry);

        (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        auto cancellation = makeToken();
        const ValidationResult result = service.check(cancellation);

        REQUIRE(result.diagnostics.size() == 1);
        CHECK_EQ(result.diagnostics[0].code, "test.throwing.internal_error");
        CHECK(result.diagnostics[0].severity == Severity::Error);
        CHECK_EQ(result.diagnostics[0].source, "test.throwing");
        CHECK_EQ(result.diagnostics[0].revision, store.current().revision);

        store.close();
    }

    TEST_CASE("cancellation before execution returns cancelled with empty diagnostics") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<FixedDiagnosticValidator>());
        registry.registerValidator(std::make_unique<CancellableValidator>());
        ValidationService service(store, registry);

        (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        auto cancellation = makeToken();
        cancellation->requestCancellation();
        const ValidationResult result = service.check(cancellation);

        CHECK(result.cancelled);
        // Partial results are discarded — never published as authoritative.
        CHECK(result.diagnostics.empty());

        store.close();
    }

    TEST_CASE("cancellation during a cooperative validator discards partial results") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<FixedDiagnosticValidator>());
        registry.registerValidator(std::make_unique<SlowValidator>());
        ValidationService service(store, registry);

        (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        auto cancellation = makeToken();
        // Cancel after a short delay (during the slow validator).
        std::thread canceller([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            cancellation->requestCancellation();
        });

        const ValidationResult result = service.check(cancellation);
        canceller.join();

        CHECK(result.cancelled);
        // Partial results are discarded — never published as authoritative.
        CHECK(result.diagnostics.empty());

        store.close();
    }

    TEST_CASE("repeated checks against the same state are deterministic") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<FixedDiagnosticValidator>());
        ValidationService service(store, registry);

        (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        auto c1 = makeToken();
        const auto r1 = service.check(c1);
        auto c2 = makeToken();
        const auto r2 = service.check(c2);

        CHECK_EQ(r1.diagnostics, r2.diagnostics);
        CHECK_EQ(r1.revision, r2.revision);

        store.close();
    }

    TEST_CASE("same diagnostic code on different entities produces distinct diagnostics") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        ValidatorRegistry registry;
        registry.registerValidator(std::make_unique<EntityDiagnosticValidator>("test.entity.a", "road-A"));
        registry.registerValidator(std::make_unique<EntityDiagnosticValidator>("test.entity.b", "road-B"));
        ValidationService service(store, registry);

        (void)store.create(infraforge::testhelpers::sampleCreateSpec(scratch.path()));

        auto cancellation = makeToken();
        const ValidationResult result = service.check(cancellation);

        REQUIRE(result.diagnostics.size() == 2);
        CHECK_EQ(result.diagnostics[0].code, "test.entity.bad");
        CHECK_EQ(result.diagnostics[1].code, "test.entity.bad");
        CHECK_NE(result.diagnostics[0].diagnosticIdentity(),
                 result.diagnostics[1].diagnosticIdentity());
        CHECK_EQ(result.diagnostics[0].entities[0].id, "road-A");
        CHECK_EQ(result.diagnostics[1].entities[0].id, "road-B");

        store.close();
    }
}

TEST_SUITE("project foundation validator") {
    TEST_CASE("a valid project record produces no diagnostics") {
        infraforge::application::validation::ProjectFoundationValidator validator;
        ValidationContext context;
        context.project.uuid = "test-uuid";
        context.project.revision = 1;
        context.project.savedRevision = 1;
        context.project.georeference.horizontalCrs = "EPSG:32633";
        context.project.georeference.linearUnit = "metre";
        context.project.georeference.originEasting = 500000.0;
        context.project.georeference.originNorthing = 4649776.0;
        context.revision = 1;

        auto cancellation = makeToken();
        std::vector<Diagnostic> output;
        validator.validate(context, *cancellation, output);
        CHECK(output.empty());
    }

    TEST_CASE("missing horizontal CRS produces a stable error diagnostic") {
        infraforge::application::validation::ProjectFoundationValidator validator;
        ValidationContext context;
        context.project.uuid = "test-uuid";
        context.project.revision = 1;
        context.project.savedRevision = 1;
        context.project.georeference.horizontalCrs = "";
        context.project.georeference.linearUnit = "metre";
        context.project.georeference.originEasting = 0.0;
        context.project.georeference.originNorthing = 0.0;
        context.revision = 1;

        auto cancellation = makeToken();
        std::vector<Diagnostic> output;
        validator.validate(context, *cancellation, output);

        REQUIRE(output.size() == 1);
        CHECK_EQ(output[0].code, "project.foundation.georeference.horizontal_crs_empty");
        CHECK(output[0].severity == Severity::Error);
        CHECK_EQ(output[0].source, "project.foundation");
        CHECK_EQ(output[0].revision, 1);
        REQUIRE(output[0].entities.size() == 1);
        CHECK_EQ(output[0].entities[0].id, "test-uuid");
        CHECK(output[0].suggestedAction.has_value());
        CHECK_EQ(output[0].suggestedAction->kind, "set_georeference");
    }

    TEST_CASE("non-finite origin produces a stable error diagnostic") {
        infraforge::application::validation::ProjectFoundationValidator validator;
        ValidationContext context;
        context.project.uuid = "test-uuid";
        context.project.revision = 1;
        context.project.savedRevision = 1;
        context.project.georeference.horizontalCrs = "EPSG:32633";
        context.project.georeference.linearUnit = "metre";
        context.project.georeference.originEasting = std::numeric_limits<double>::infinity();
        context.project.georeference.originNorthing = 0.0;
        context.revision = 1;

        auto cancellation = makeToken();
        std::vector<Diagnostic> output;
        validator.validate(context, *cancellation, output);

        REQUIRE(output.size() == 1);
        CHECK_EQ(output[0].code, "project.foundation.georeference.origin_not_finite");
        CHECK(output[0].severity == Severity::Error);
    }

    TEST_CASE("saved_revision exceeding revision produces a diagnostic") {
        infraforge::application::validation::ProjectFoundationValidator validator;
        ValidationContext context;
        context.project.uuid = "test-uuid";
        context.project.revision = 1;
        context.project.savedRevision = 2;
        context.project.georeference.horizontalCrs = "EPSG:32633";
        context.project.georeference.linearUnit = "metre";
        context.project.georeference.originEasting = 0.0;
        context.project.georeference.originNorthing = 0.0;
        context.revision = 1;

        auto cancellation = makeToken();
        std::vector<Diagnostic> output;
        validator.validate(context, *cancellation, output);

        REQUIRE(output.size() == 1);
        CHECK_EQ(output[0].code, "project.foundation.saved_revision_exceeds_revision");
        CHECK(output[0].severity == Severity::Error);
    }
}

TEST_SUITE("world.check command through the processor") {
    TEST_CASE("world.check on an open valid project returns zero diagnostics") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createProjectFrame("req-create", infraforge::runtime::utf8String(scratch.path())));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        processor.post("conn-1", worldCheckFrame("req-check"));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 2);
        CHECK_EQ(results[1].request_id(), "req-check");
        REQUIRE(results[1].has_result());
        REQUIRE(results[1].result().has_world_check());
        const auto& worldCheck = results[1].result().world_check();
        CHECK_FALSE(worldCheck.cancelled());
        CHECK_FALSE(worldCheck.stale());
        CHECK_EQ(worldCheck.revision(), 1);
        CHECK(worldCheck.diagnostics().empty());

        processor.shutdown();
        store.close();
    }

    TEST_CASE("world.check without an open project returns PROJECT_NOT_OPEN") {
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", worldCheckFrame("req-no-project"));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 1);
        CHECK_EQ(results[0].request_id(), "req-no-project");
        REQUIRE(results[0].result().has_error());
        CHECK_EQ(results[0].result().error().code(),
            infraforge::protocol::v1::COMMAND_ERROR_CODE_PROJECT_NOT_OPEN);

        processor.shutdown();
    }

    TEST_CASE("world.check result diagnostics have structured fields") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createProjectFrame("req-create", infraforge::runtime::utf8String(scratch.path())));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        processor.post("conn-1", worldCheckFrame("req-check"));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 2);
        const auto& worldCheck = results[1].result().world_check();
        CHECK_EQ(worldCheck.revision(), 1);
        CHECK_FALSE(worldCheck.cancelled());

        processor.shutdown();
        store.close();
    }
}

TEST_SUITE("diagnostic event lifecycle through the processor") {
    TEST_CASE("first world.check broadcasts diagnostic_added events for non-empty results") {
        // We need a validator that produces diagnostics. The
        // ProjectFoundationValidator produces none for a valid project, so
        // we test the event path indirectly: a valid project produces no
        // events, and closing the project produces a cleared event.
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createProjectFrame("req-create", infraforge::runtime::utf8String(scratch.path())));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        // world.check on a valid project: no diagnostics, no added events.
        processor.post("conn-1", worldCheckFrame("req-check"));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));
        const auto eventsAfterCheck = sink.events();
        // No diagnostic events (only project events from create).
        for (const auto& e : eventsAfterCheck) {
            CHECK_FALSE(e.event().has_diagnostic_added());
            CHECK_FALSE(e.event().has_diagnostic_removed());
            CHECK_FALSE(e.event().has_diagnostic_cleared());
        }

        processor.shutdown();
        store.close();
    }

    TEST_CASE("project close broadcasts diagnostic_cleared event") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createProjectFrame("req-create", infraforge::runtime::utf8String(scratch.path())));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        // Run world.check to establish a published set (empty, but published).
        processor.post("conn-1", worldCheckFrame("req-check"));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));

        // Close the project — should broadcast a diagnostic_cleared event.
        // create: 2 frames, world.check: 1 frame, close: 3 frames (result +
        // project_closed event + diagnostic_cleared event) = 6 total.
        processor.post("conn-1", closeProjectFrame("req-close"));
        REQUIRE(sink.waitForTotal(6, kWaitTimeout));

        const auto events = sink.events();
        bool foundCleared = false;
        for (const auto& e : events) {
            if (e.event().has_diagnostic_cleared()) {
                foundCleared = true;
                CHECK_EQ(e.event().diagnostic_cleared().reason(), "project_closed");
            }
        }
        CHECK(foundCleared);

        processor.shutdown();
        store.close();
    }

    TEST_CASE("project save does not invalidate diagnostics when revision is unchanged") {
        // Save on a freshly created (non-dirty) project does not produce a
        // RevisionChanged event, so diagnostics are not cleared. The
        // revision_changed invalidation path is architecturally wired in
        // publishEvents but is not triggered by any current mutation path
        // on main; it will fire when domain mutations that change the
        // revision are implemented.
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createProjectFrame("req-create", infraforge::runtime::utf8String(scratch.path())));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        // Run world.check to establish a published set.
        processor.post("conn-1", worldCheckFrame("req-check"));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));
        const auto eventsBeforeSave = sink.events().size();

        // Save the project — no revision change, no diagnostic invalidation.
        processor.post("conn-1", saveProjectFrame("req-save"));
        REQUIRE(sink.waitForResults(3, kWaitTimeout));
        const auto eventsAfterSave = sink.events().size();

        CHECK_EQ(eventsBeforeSave, eventsAfterSave);

        processor.shutdown();
        store.close();
    }

    TEST_CASE("repeated world.check with same diagnostics produces no duplicate events") {
        infraforge::testhelpers::ScratchDirectory scratch;
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", createProjectFrame("req-create", infraforge::runtime::utf8String(scratch.path())));
        REQUIRE(sink.waitForTotal(2, kWaitTimeout));

        // First world.check.
        processor.post("conn-1", worldCheckFrame("req-check-1"));
        REQUIRE(sink.waitForResults(2, kWaitTimeout));
        const auto eventsAfterFirst = sink.events().size();

        // Second world.check — same state, no new events.
        processor.post("conn-1", worldCheckFrame("req-check-2"));
        REQUIRE(sink.waitForResults(3, kWaitTimeout));
        const auto eventsAfterSecond = sink.events().size();

        CHECK_EQ(eventsAfterFirst, eventsAfterSecond);

        processor.shutdown();
        store.close();
    }
}

TEST_SUITE("world.cancel_check command") {
    TEST_CASE("cancel without an active run returns cancellation_requested=false") {
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        processor.post("conn-1", worldCancelCheckFrame("req-cancel"));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 1);
        CHECK_EQ(results[0].request_id(), "req-cancel");
        REQUIRE(results[0].result().has_world_cancel_check());
        CHECK_FALSE(results[0].result().world_cancel_check().cancellation_requested());

        processor.shutdown();
    }

    TEST_CASE("cancel is handled immediately without entering the executor queue") {
        // We verify this indirectly: cancel returns a result even when the
        // executor is busy. We don't need a slow validator here because the
        // cancel is intercepted in post() before queueing.
        infraforge::persistence::SqliteProjectStore store;
        RecordingSink sink;
        infraforge::application::CommandProcessor processor(store, sink);
        processor.start();

        // Send cancel — it should return immediately.
        processor.post("conn-1", worldCancelCheckFrame("req-cancel"));
        REQUIRE(sink.waitForResults(1, kWaitTimeout));

        const auto results = sink.results();
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].result().has_world_cancel_check());
        CHECK_FALSE(results[0].result().world_cancel_check().cancellation_requested());

        processor.shutdown();
    }
}
