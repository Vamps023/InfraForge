# Command model

Commands are versioned application requests. Names are domain-qualified.

## Foundation command families

### Session

- `session.hello`
- `session.ping`

### Project lifecycle

- `project.create`
- `project.open`
- `project.save`
- `project.save_as`
- `project.close`
- `project.get_summary`

### Editing support

- `edit.undo`
- `edit.redo`
- `selection.resolve` for canonical selection details where required

### Validation/jobs

- `world.check`
- `job.cancel`

Domain commands are added with their production domain implementation. A command name must not be committed with a production route that only returns a placeholder success.

## Command handling rule

`transport decode -> validation -> application command handler -> domain/persistence transaction -> result/event`

Transport code cannot mutate project state directly.