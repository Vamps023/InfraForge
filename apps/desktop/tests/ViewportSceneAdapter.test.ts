import { describe, it, expect } from 'vitest'
import { adaptTerrainScene, emptyViewportScene } from '../src/ViewportSceneAdapter'

describe('ViewportSceneAdapter', () => {
  describe('adaptTerrainScene', () => {
    it('maps protobuf camelCase fields to viewport expected field names', () => {
      const engineScene = {
        originEasting: 500000,
        originNorthing: 4650000,
        originHeight: 100,
        tiles: [
          {
            datasetUuid: 'ds-1',
            datasetRevision: 42n,
            chunkX: 10n,
            chunkY: -5n,
            absolutePath: 'C:/data/tile.iforgetile',
            minEasting: 500000,
            minNorthing: 4650000,
            maxEasting: 501000,
            maxNorthing: 4651000,
          },
        ],
        missingTiles: 3n,
        revision: 7n,
      }

      const result = adaptTerrainScene(engineScene)
      expect(result).not.toBeNull()
      expect(result!.type).toBe('scene')
      expect(result!.originEasting).toBe(500000)
      expect(result!.originNorthing).toBe(4650000)
      expect(result!.originHeight).toBe(100)
      expect(result!.missingTiles).toBe('3')
      expect(result!.revision).toBe('7')
      expect(result!.tiles).toHaveLength(1)
      const tile = result!.tiles[0]!
      expect(tile.datasetUuid).toBe('ds-1')
      expect(tile.datasetRevision).toBe('42')
      expect(tile.chunkX).toBe('10')
      expect(tile.chunkY).toBe('-5')
      // BLOCKER 6: viewport expects 'path', not 'absolutePath'
      expect(tile.path).toBe('C:/data/tile.iforgetile')
      expect(tile.absolutePath).toBeUndefined()
      // BLOCKER 6: viewport expects 'minE'/'minN'/'maxE'/'maxN', not minEasting etc.
      expect(tile.minE).toBe(500000)
      expect(tile.minN).toBe(4650000)
      expect(tile.maxE).toBe(501000)
      expect(tile.maxN).toBe(4651000)
      expect(tile.minEasting).toBeUndefined()
      expect(tile.minNorthing).toBeUndefined()
      expect(tile.maxEasting).toBeUndefined()
      expect(tile.maxNorthing).toBeUndefined()
    })

    it('handles multiple tiles', () => {
      const engineScene = {
        originEasting: 0,
        originNorthing: 0,
        originHeight: 0,
        tiles: [
          {
            datasetUuid: 'a',
            datasetRevision: 1n,
            chunkX: 0n,
            chunkY: 0n,
            absolutePath: '/a.tif',
            minEasting: 0,
            minNorthing: 0,
            maxEasting: 1000,
            maxNorthing: 1000,
          },
          {
            datasetUuid: 'b',
            datasetRevision: 2n,
            chunkX: 1n,
            chunkY: 0n,
            absolutePath: '/b.tif',
            minEasting: 1000,
            minNorthing: 0,
            maxEasting: 2000,
            maxNorthing: 1000,
          },
        ],
        missingTiles: 0n,
        revision: 1n,
      }

      const result = adaptTerrainScene(engineScene)
      expect(result).not.toBeNull()
      expect(result!.tiles).toHaveLength(2)
      expect(result!.tiles[0]!.path).toBe('/a.tif')
      expect(result!.tiles[1]!.path).toBe('/b.tif')
    })

    it('handles string-valued BigInt fields', () => {
      const engineScene = {
        originEasting: 0,
        originNorthing: 0,
        originHeight: 0,
        tiles: [
          {
            datasetUuid: 'x',
            datasetRevision: '999',
            chunkX: '-100',
            chunkY: '200',
            absolutePath: '/x.tif',
            minEasting: 0,
            minNorthing: 0,
            maxEasting: 500,
            maxNorthing: 500,
          },
        ],
        missingTiles: '0',
        revision: '1',
      }

      const result = adaptTerrainScene(engineScene)
      expect(result).not.toBeNull()
      expect(result!.tiles[0]!.datasetRevision).toBe('999')
      expect(result!.tiles[0]!.chunkX).toBe('-100')
      expect(result!.tiles[0]!.chunkY).toBe('200')
    })

    it('returns null for malformed input', () => {
      expect(adaptTerrainScene(null)).toBeNull()
      expect(adaptTerrainScene(undefined)).toBeNull()
      expect(adaptTerrainScene('string')).toBeNull()
      expect(adaptTerrainScene([])).toBeNull()
      expect(adaptTerrainScene({})).toBeNull()
      expect(adaptTerrainScene({ originEasting: 0 })).toBeNull()
      expect(adaptTerrainScene({ originEasting: 0, originNorthing: 0 })).toBeNull()
      expect(
        adaptTerrainScene({ originEasting: 0, originNorthing: 0, tiles: 'not-array' }),
      ).toBeNull()
    })

    it('preserves 64-bit values beyond Number.MAX_SAFE_INTEGER as strings', () => {
      const huge = BigInt(Number.MAX_SAFE_INTEGER) + 1n
      const engineScene = {
        originEasting: 0,
        originNorthing: 0,
        originHeight: 0,
        tiles: [
          {
            datasetUuid: 'big',
            datasetRevision: huge,
            chunkX: huge,
            chunkY: -huge,
            absolutePath: '/big.tif',
            minEasting: 0,
            minNorthing: 0,
            maxEasting: 1,
            maxNorthing: 1,
          },
        ],
        missingTiles: huge,
        revision: huge,
      }

      const result = adaptTerrainScene(engineScene)
      expect(result).not.toBeNull()
      const expected = huge.toString()
      expect(result!.tiles[0]!.datasetRevision).toBe(expected)
      expect(result!.tiles[0]!.chunkX).toBe(expected)
      expect(result!.tiles[0]!.chunkY).toBe('-' + expected)
      expect(result!.missingTiles).toBe(expected)
      expect(result!.revision).toBe(expected)
    })
  })

  describe('emptyViewportScene', () => {
    it('produces a complete valid empty scene with all required fields', () => {
      const scene = emptyViewportScene()
      expect(scene.type).toBe('scene')
      expect(scene.originEasting).toBe(0)
      expect(scene.originNorthing).toBe(0)
      expect(scene.originHeight).toBe(0)
      expect(scene.tiles).toEqual([])
      // BLOCKER 7: missingTiles and revision must be present
      expect(scene.missingTiles).toBe('0')
      expect(scene.revision).toBe('0')
    })

    it('produces a scene that can be JSON serialized without loss', () => {
      const scene = emptyViewportScene()
      const json = JSON.stringify(scene)
      const parsed = JSON.parse(json)
      expect(parsed.type).toBe('scene')
      expect(parsed.originEasting).toBe(0)
      expect(parsed.originNorthing).toBe(0)
      expect(parsed.originHeight).toBe(0)
      expect(parsed.tiles).toEqual([])
      expect(parsed.missingTiles).toBe('0')
      expect(parsed.revision).toBe('0')
    })
  })

  // BLOCKER 20: Golden JSON fixture test that proves the exact chain:
  // engine TerrainSceneResult → desktop adapter JSON → native parser expectations.
  // This test catches future field-name drift.
  describe('golden JSON fixture (BLOCKER 20)', () => {
    it('produces JSON matching the viewport native parser contract', () => {
      // Golden fixture: a scene with all field types represented.
      const engineScene = {
        originEasting: 500000,
        originNorthing: 4650000,
        originHeight: 100,
        tiles: [
          {
            datasetUuid: 'ds-001',
            datasetRevision: 12345n,
            chunkX: 10n,
            chunkY: -5n,
            absolutePath: 'C:/projects/test/.iforge/terrain/tiles/ds-001_10_-5.iforgetile',
            minEasting: 500000,
            minNorthing: 4650000,
            maxEasting: 501000,
            maxNorthing: 4651000,
          },
          {
            datasetUuid: 'ds-002',
            datasetRevision: 67890n,
            chunkX: -100n,
            chunkY: 200n,
            absolutePath: 'C:/projects/test/.iforge/terrain/tiles/ds-002_-100_200.iforgetile',
            minEasting: 499000,
            minNorthing: 4649000,
            maxEasting: 500000,
            maxNorthing: 4650000,
          },
        ],
        missingTiles: 3n,
        revision: 42n,
      }

      const result = adaptTerrainScene(engineScene)
      expect(result).not.toBeNull()

      // Serialize to JSON (this is what crosses the IPC boundary).
      const json = JSON.stringify(result)
      const parsed = JSON.parse(json)

      // Verify the exact field names the native viewport parser expects.
      expect(parsed.type).toBe('scene')
      expect(parsed.originEasting).toBe(500000)
      expect(parsed.originNorthing).toBe(4650000)
      expect(parsed.originHeight).toBe(100)
      expect(parsed.missingTiles).toBe('3')
      expect(parsed.revision).toBe('42')

      // Tile 0: verify all viewport-expected field names.
      const tile0 = parsed.tiles[0]
      expect(tile0.datasetUuid).toBe('ds-001')
      expect(tile0.datasetRevision).toBe('12345')
      expect(tile0.chunkX).toBe('10')
      expect(tile0.chunkY).toBe('-5')
      expect(tile0.path).toBe('C:/projects/test/.iforge/terrain/tiles/ds-001_10_-5.iforgetile')
      expect(tile0.minE).toBe(500000)
      expect(tile0.minN).toBe(4650000)
      expect(tile0.maxE).toBe(501000)
      expect(tile0.maxN).toBe(4651000)

      // Verify protobuf field names are NOT present (no drift).
      expect(tile0.absolutePath).toBeUndefined()
      expect(tile0.minEasting).toBeUndefined()
      expect(tile0.minNorthing).toBeUndefined()
      expect(tile0.maxEasting).toBeUndefined()
      expect(tile0.maxNorthing).toBeUndefined()

      // Tile 1: verify signed chunk coordinates.
      const tile1 = parsed.tiles[1]
      expect(tile1.chunkX).toBe('-100')
      expect(tile1.chunkY).toBe('200')
      expect(tile1.path).toBe('C:/projects/test/.iforge/terrain/tiles/ds-002_-100_200.iforgetile')
    })

    it('empty scene golden fixture matches native parser expectations', () => {
      const scene = emptyViewportScene()
      const json = JSON.stringify(scene)
      const parsed = JSON.parse(json)

      // The native parser must handle an empty scene with all fields.
      expect(parsed.type).toBe('scene')
      expect(parsed.originEasting).toBe(0)
      expect(parsed.originNorthing).toBe(0)
      expect(parsed.originHeight).toBe(0)
      expect(Array.isArray(parsed.tiles)).toBe(true)
      expect(parsed.tiles).toHaveLength(0)
      expect(parsed.missingTiles).toBe('0')
      expect(parsed.revision).toBe('0')
    })

    it('preserves BigInt values beyond MAX_SAFE_INTEGER in JSON', () => {
      const huge = BigInt(Number.MAX_SAFE_INTEGER) + 1n
      const engineScene = {
        originEasting: 0,
        originNorthing: 0,
        originHeight: 0,
        tiles: [
          {
            datasetUuid: 'big',
            datasetRevision: huge,
            chunkX: huge,
            chunkY: -huge,
            absolutePath: '/big.tif',
            minEasting: 0,
            minNorthing: 0,
            maxEasting: 1,
            maxNorthing: 1,
          },
        ],
        missingTiles: huge,
        revision: huge,
      }

      const result = adaptTerrainScene(engineScene)
      const json = JSON.stringify(result)
      const parsed = JSON.parse(json)

      // BigInt values must survive JSON round-trip as decimal strings.
      const expected = huge.toString()
      expect(parsed.tiles[0].datasetRevision).toBe(expected)
      expect(parsed.tiles[0].chunkX).toBe(expected)
      expect(parsed.tiles[0].chunkY).toBe('-' + expected)
      expect(parsed.missingTiles).toBe(expected)
      expect(parsed.revision).toBe(expected)
    })
  })
})
