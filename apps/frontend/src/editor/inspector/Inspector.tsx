import { useSyncExternalStore, useState } from 'react'
import {
  ChevronDown,
  ChevronRight,
  MousePointer,
  Layers,
  GitFork,
  Route,
  MapPin,
  Trash2,
  Crosshair,
  Search,
} from 'lucide-react'
import { inspectorSectionRegistry } from './inspectorRegistry'
import { useSelectionStore } from '../selection/selectionStore'
import { useRoadStore } from '../../features/road/roadStore'
import { useProjectStore } from '../../features/project/projectStore'
import { RoadLaneEditor } from '../../features/road/RoadLaneEditor'
import { deleteRoad } from '../../features/road/roadApi'
import type { EngineClient } from '../../lib/engineSession'

export interface InspectorProps {
  getEngineClient?: () => EngineClient | null
  onOpenGeoreference?: () => void
}

export type InspectorTab = 'selection' | 'lanes' | 'junctions' | 'roads' | 'location'

export function Inspector({ getEngineClient, onOpenGeoreference }: InspectorProps) {
  const [activeTab, setActiveTab] = useState<InspectorTab>('selection')
  const [roadSearch, setRoadSearch] = useState('')

  const selectedIds = useSelectionStore((state) => state.selectedIds)
  const primaryId = useSelectionStore((state) => state.primaryId)
  const select = useSelectionStore((state) => state.select)

  const roads = useRoadStore((state) => state.roads)
  const selectedRoadId = useRoadStore((state) => state.selectedRoadId)
  const selectedRoad = roads.find((r) => r.roadId === selectedRoadId)
  const roadDetails = useRoadStore((state) => state.details)
  const junctions = useRoadStore((state) => state.junctions)
  const selectedJunctionId = useRoadStore((state) => state.selectedJunctionId)

  const projectSummary = useProjectStore((state) => state.summary)

  const sections = useSyncExternalStore(
    inspectorSectionRegistry.subscribe,
    inspectorSectionRegistry.getSnapshot,
  )
  const context = { selectedIds, primaryId }
  const applicable = sections.filter((section) => section.applies(context))

  const filteredRoads = roads.filter((r) =>
    r.name.toLowerCase().includes(roadSearch.toLowerCase()) ||
    r.roadId.toLowerCase().includes(roadSearch.toLowerCase()),
  )

  const handleDeleteRoad = async (roadId: string) => {
    const client = getEngineClient?.()
    if (client) {
      await deleteRoad(client, roadId).catch(() => undefined)
    }
  }

  return (
    <aside className="panel inspector-panel" aria-label="Inspector">
      {/* Tab bar header matching OpenGeoStudio */}
      <div className="inspector-tab-bar" role="tablist" aria-label="Inspector tabs">
        <button
          type="button"
          role="tab"
          aria-selected={activeTab === 'selection'}
          className={`inspector-tab-btn${activeTab === 'selection' ? ' active' : ''}`}
          onClick={() => setActiveTab('selection')}
          title="Selection Properties"
        >
          <MousePointer size={12} />
          <span>Selection</span>
        </button>

        <button
          type="button"
          role="tab"
          aria-selected={activeTab === 'lanes'}
          className={`inspector-tab-btn${activeTab === 'lanes' ? ' active' : ''}`}
          onClick={() => setActiveTab('lanes')}
          title="Lane Sections & Cross Section"
        >
          <Layers size={12} />
          <span>Lanes</span>
        </button>

        <button
          type="button"
          role="tab"
          aria-selected={activeTab === 'junctions'}
          className={`inspector-tab-btn${activeTab === 'junctions' ? ' active' : ''}`}
          onClick={() => setActiveTab('junctions')}
          title="Junction Topology"
        >
          <GitFork size={12} />
          <span>Junctions</span>
          {junctions.length > 0 && <span className="tab-count-badge">{junctions.length}</span>}
        </button>

        <button
          type="button"
          role="tab"
          aria-selected={activeTab === 'roads'}
          className={`inspector-tab-btn${activeTab === 'roads' ? ' active' : ''}`}
          onClick={() => setActiveTab('roads')}
          title="All Project Roads"
        >
          <Route size={12} />
          <span>Roads</span>
          {roads.length > 0 && <span className="tab-count-badge">{roads.length}</span>}
        </button>

        <button
          type="button"
          role="tab"
          aria-selected={activeTab === 'location'}
          className={`inspector-tab-btn${activeTab === 'location' ? ' active' : ''}`}
          onClick={() => setActiveTab('location')}
          title="Project CRS & Location"
        >
          <MapPin size={12} />
          <span>Location</span>
        </button>
      </div>

      <div className="inspector-body">
        {/* Tab 1: Selection */}
        {activeTab === 'selection' && (
          <div className="inspector-tab-pane">
            {applicable.length === 0 ? (
              <div className="panel-empty">
                {selectedIds.length === 0
                  ? 'Select an authored entity to inspect its properties.'
                  : 'No inspector section is available for the current selection.'}
              </div>
            ) : (
              applicable.map((section) => (
                <CollapsibleSection key={section.id} label={section.label}>
                  {section.render(context)}
                </CollapsibleSection>
              ))
            )}
          </div>
        )}

        {/* Tab 2: Lanes */}
        {activeTab === 'lanes' && (
          <div className="inspector-tab-pane">
            {selectedRoad && roadDetails && roadDetails.roadId === selectedRoad.roadId ? (
              <RoadLaneEditor
                road={selectedRoad}
                details={roadDetails}
                getEngineClient={getEngineClient ?? (() => null)}
              />
            ) : (
              <div className="panel-empty ogs-empty-card">
                <span className="step-badge-pill">NEXT STEP</span>
                <p><strong>Select a road to edit its lanes</strong></p>
                <p className="empty-card-subtext">
                  Choose Select (V), click a road on the canvas or pick from the Roads tab to edit its cross section.
                </p>
              </div>
            )}
          </div>
        )}

        {/* Tab 3: Junctions */}
        {activeTab === 'junctions' && (
          <div className="inspector-tab-pane">
            <div className="pane-section-header">
              <span className="pane-section-title">Network Junctions ({junctions.length})</span>
            </div>
            {junctions.length === 0 ? (
              <div className="panel-empty">
                No junctions in network. Connect two roads at endpoints to create an intersection.
              </div>
            ) : (
              <div className="inspector-items-list">
                {junctions.map((j) => {
                  const isSelected = selectedJunctionId === j.junctionId
                  return (
                    <div
                      key={j.junctionId}
                      className={`inspector-item-card${isSelected ? ' selected' : ''}`}
                      onClick={() => select([`junction:${j.junctionId}`])}
                    >
                      <div className="item-card-title-row">
                        <GitFork size={14} className="item-card-icon" />
                        <span className="item-card-name">{j.name || 'Junction'}</span>
                        <span className="badge compact">{j.type || 'priority'}</span>
                      </div>
                      <div className="item-card-details">
                        <span>{j.approaches.length} approaches</span>
                        <span>({j.posX.toFixed(1)}, {j.posY.toFixed(1)})</span>
                      </div>
                    </div>
                  )
                })}
              </div>
            )}
          </div>
        )}

        {/* Tab 4: Roads */}
        {activeTab === 'roads' && (
          <div className="inspector-tab-pane">
            <div className="search-input-box">
              <Search size={13} />
              <input
                type="text"
                placeholder="Filter roads…"
                value={roadSearch}
                onChange={(e) => setRoadSearch(e.target.value)}
              />
            </div>

            {filteredRoads.length === 0 ? (
              <div className="panel-empty">
                {roads.length === 0 ? 'No roads in project.' : 'No matching roads found.'}
              </div>
            ) : (
              <div className="inspector-items-list">
                {filteredRoads.map((r) => {
                  const isSelected = selectedRoadId === r.roadId
                  const kind = r.constructionKind || 'fitted'
                  return (
                    <div
                      key={r.roadId}
                      className={`inspector-item-card${isSelected ? ' selected' : ''}`}
                      onClick={() => select([`road:${r.roadId}`])}
                    >
                      <div className="item-card-title-row">
                        <Route size={14} className="item-card-icon" />
                        <span className="item-card-name" title={r.name}>{r.name}</span>
                        <span className="badge compact">{kind}</span>
                        <button
                          type="button"
                          className="item-delete-btn"
                          title="Delete road"
                          onClick={(e) => {
                            e.stopPropagation()
                            void handleDeleteRoad(r.roadId)
                          }}
                        >
                          <Trash2 size={12} />
                        </button>
                      </div>
                      <div className="item-card-details">
                        <span>{r.length >= 1000 ? `${(r.length / 1000).toFixed(2)} km` : `${r.length.toFixed(1)} m`}</span>
                        <span>{r.alignmentSegmentCount} segs</span>
                      </div>
                    </div>
                  )
                })}
              </div>
            )}
          </div>
        )}

        {/* Tab 5: Location */}
        {activeTab === 'location' && (
          <div className="inspector-tab-pane">
            {projectSummary ? (
              <div className="location-summary-card">
                <div className="location-field-row">
                  <span className="field-label">CRS</span>
                  <span className="badge crs-badge">{projectSummary.georeference?.horizontalCrs || 'Unspecified'}</span>
                </div>
                <div className="location-field-row">
                  <span className="field-label">Traffic Side</span>
                  <span className="badge traffic-badge">
                    {projectSummary.trafficSide === 1 ? 'Left-Hand (LHT)' : 'Right-Hand (RHT)'}
                  </span>
                </div>
                <div className="location-field-row">
                  <span className="field-label">Origin Easting</span>
                  <span className="mono-val">{projectSummary.georeference?.originEasting.toFixed(3) ?? '0.000'}</span>
                </div>
                <div className="location-field-row">
                  <span className="field-label">Origin Northing</span>
                  <span className="mono-val">{projectSummary.georeference?.originNorthing.toFixed(3) ?? '0.000'}</span>
                </div>
                <div className="location-field-row">
                  <span className="field-label">Origin Elevation</span>
                  <span className="mono-val">{projectSummary.georeference?.originHeight.toFixed(3) ?? '0.000'} m</span>
                </div>

                {onOpenGeoreference && (
                  <button
                    type="button"
                    className="button outline full-width"
                    style={{ marginTop: '12px' }}
                    onClick={onOpenGeoreference}
                  >
                    Edit Georeference Settings
                  </button>
                )}
              </div>
            ) : (
              <div className="panel-empty">No project is open.</div>
            )}
          </div>
        )}
      </div>
    </aside>
  )
}

function CollapsibleSection({
  label,
  children,
}: {
  label: string
  children: React.ReactNode
}) {
  const [expanded, setExpanded] = useState(true)
  const sectionId = `inspector-section-${label.replace(/\s+/g, '-').toLowerCase()}`

  return (
    <section className="inspector-section">
      <button
        type="button"
        className="inspector-section-title"
        aria-expanded={expanded}
        aria-controls={sectionId}
        onClick={() => setExpanded(!expanded)}
      >
        {expanded ? <ChevronDown size={12} /> : <ChevronRight size={12} />}
        <span>{label}</span>
      </button>
      {expanded ? (
        <div className="inspector-section-body" id={sectionId}>
          {children}
        </div>
      ) : null}
    </section>
  )
}
