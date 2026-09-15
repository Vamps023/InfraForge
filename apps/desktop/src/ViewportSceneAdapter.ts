// Explicit adapter that maps the engine's TerrainSceneResult (protobuf-shaped
// camelCase fields) to the native viewport's scene control DTO.
//
// BLOCKER 6: The engine/frontend protobuf scene uses fields like
// `absolutePath`, `minEasting`, `minNorthing`, `maxEasting`, `maxNorthing`,
// but the native viewport parser expects `path`, `minE`, `minN`, `maxE`,
// `maxN`. This adapter performs the deliberate field mapping so the two
// contracts never silently drift.
//
// 64-bit integer fields (datasetRevision, chunkX, chunkY, missingTiles,
// revision) are preserved as BigInt or string so JSON transport is lossless.

/** Input shape matching the protobuf TerrainSceneResult (camelCase). */
export interface EngineTerrainSceneTile {
  datasetUuid: string
  datasetRevision: bigint | string | number
  chunkX: bigint | string | number
  chunkY: bigint | string | number
  absolutePath: string
  minEasting: number
  minNorthing: number
  maxEasting: number
  maxNorthing: number
}

export interface EngineTerrainScene {
  originEasting: number
  originNorthing: number
  originHeight: number
  tiles: EngineTerrainSceneTile[]
  missingTiles: bigint | string | number
  revision: bigint | string | number
}

/** Output shape matching the native viewport's TerrainScene parser. */
export interface ViewportSceneTile {
  datasetUuid: string
  datasetRevision: string
  chunkX: string
  chunkY: string
  path: string
  minE: number
  minN: number
  maxE: number
  maxN: number
}

export interface ViewportRoadSceneVertex {
  x: number
  y: number
  z: number
  nx?: number
  ny?: number
  nz?: number
}

export interface ViewportRoadSceneMesh {
  roadId: string
  vertices: ViewportRoadSceneVertex[]
  indices: number[]
}

export interface ViewportSceneControl {
  type: 'scene'
  originEasting: number
  originNorthing: number
  originHeight: number
  tiles: ViewportSceneTile[]
  missingTiles: string
  revision: string
  // Optional road scene data for the viewport renderer.
  roads?: ViewportRoadSceneMesh[]
  roadRevision?: string
}

function toBigIntString(value: bigint | string | number): string {
  if (typeof value === 'bigint') {
    return value.toString()
  }
  return String(value)
}

function toTile(tile: EngineTerrainSceneTile): ViewportSceneTile {
  return {
    datasetUuid: tile.datasetUuid,
    datasetRevision: toBigIntString(tile.datasetRevision),
    chunkX: toBigIntString(tile.chunkX),
    chunkY: toBigIntString(tile.chunkY),
    path: tile.absolutePath,
    minE: tile.minEasting,
    minN: tile.minNorthing,
    maxE: tile.maxEasting,
    maxN: tile.maxNorthing,
  }
}

/**
 * Convert an engine TerrainSceneResult (protobuf-shaped) into the exact
 * ViewportSceneControlDTO expected by the native viewport parser.
 *
 * Returns null if the input is malformed.
 */
export function adaptTerrainScene(
  scene: unknown,
): ViewportSceneControl | null {
  if (typeof scene !== 'object' || scene === null || Array.isArray(scene)) {
    return null
  }
  const s = scene as Partial<EngineTerrainScene>
  if (!Array.isArray(s.tiles)) {
    return null
  }
  if (typeof s.originEasting !== 'number' || typeof s.originNorthing !== 'number') {
    return null
  }
  return {
    type: 'scene' as const,
    originEasting: s.originEasting,
    originNorthing: s.originNorthing,
    originHeight: s.originHeight ?? 0,
    tiles: s.tiles.map(toTile),
    missingTiles: toBigIntString(s.missingTiles ?? 0),
    revision: toBigIntString(s.revision ?? 0),
  }
}

/**
 * Attach road scene data to an existing ViewportSceneControl. The road
 * scene is an optional addition to the terrain scene; the viewport
 * renderer parses the "roads" field if present.
 */
export function attachRoadScene(
  control: ViewportSceneControl,
  roadScene: unknown,
): ViewportSceneControl {
  if (typeof roadScene !== 'object' || roadScene === null || Array.isArray(roadScene)) {
    return control
  }
  const rs = roadScene as Partial<{
    originEasting: number
    originNorthing: number
    originHeight: number
    meshes: Array<{
      roadId: string
      vertices: Array<{ x: number; y: number; z: number; nx?: number; ny?: number; nz?: number }>
      indices: number[]
    }>
    revision: bigint | string | number
  }>
  if (!Array.isArray(rs.meshes)) {
    return control
  }
  return {
    ...control,
    roads: rs.meshes.map((mesh) => ({
      roadId: mesh.roadId,
      vertices: mesh.vertices.map((v) => ({
        x: v.x,
        y: v.y,
        z: v.z,
        nx: v.nx,
        ny: v.ny,
        nz: v.nz,
      })),
      indices: mesh.indices,
    })),
    roadRevision: toBigIntString(rs.revision ?? 0),
  }
}

/**
 * Build an empty/clear scene that satisfies the viewport's full schema.
 * Used on project close/switch to release all GPU terrain.
 */
export function emptyViewportScene(): ViewportSceneControl {
  return {
    type: 'scene',
    originEasting: 0,
    originNorthing: 0,
    originHeight: 0,
    tiles: [],
    missingTiles: '0',
    revision: '0',
    roads: [],
    roadRevision: '0',
  }
}
