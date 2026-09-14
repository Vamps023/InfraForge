#!/usr/bin/env node
// Package validation script for InfraForge v0.1 Windows release.
// Verifies the electron-builder unpacked output contains every required
// runtime resource at the expected paths. Run after `npm run package:win`.
//
// Usage:
//   node scripts/validate-package.js <unpacked-dir>
//
// The unpacked dir is typically:
//   apps/desktop/dist-release/win-unpacked
//
// Exit code 0 = all checks passed, 1 = missing resources.

const fs = require('node:fs')
const path = require('node:path')

const expectedVersion = '0.1.0'

// Required native runtime DLLs (verified via dumpbin /dependents transitive
// closure). The engine and viewport share the native/ directory.
const requiredEngineDlls = [
  'abseil_dll.dll',
  'gdal.dll',
  'geotiff.dll',
  'jpeg62.dll',
  'json-c.dll',
  'liblzma.dll',
  'libpng16.dll',
  'libprotobuf.dll',
  'proj_9.dll',
  'sqlite3.dll',
  'tiff.dll',
  'z.dll',
]

const requiredViewportDlls = [
  'vulkan-1.dll',
]

// Required PROJ data files for CRS lookup and coordinate transformation.
const requiredProjData = [
  'proj.db',
  'proj.ini',
]

function checkFile(baseDir, relativePath) {
  const fullPath = path.join(baseDir, relativePath)
  const exists = fs.existsSync(fullPath)
  return {
    name: relativePath,
    passed: exists,
    detail: exists ? 'found' : 'MISSING',
  }
}

function main() {
  const unpackedDir = process.argv[2]
  if (!unpackedDir) {
    console.error('Usage: node scripts/validate-package.js <unpacked-dir>')
    process.exit(1)
  }

  if (!fs.existsSync(unpackedDir) || !fs.statSync(unpackedDir).isDirectory()) {
    console.error('Unpacked directory not found: ' + unpackedDir)
    process.exit(1)
  }

  const results = []

  // 1. Main Electron executable
  const exeName = process.platform === 'win32' ? 'InfraForge.exe' : 'InfraForge'
  results.push(checkFile(unpackedDir, exeName))

  // 2. Native engine executable
  results.push(checkFile(unpackedDir, 'resources/native/infraforge-engine.exe'))

  // 3. Native viewport executable
  results.push(checkFile(unpackedDir, 'resources/native/infraforge-viewport.exe'))

  // 4. Engine runtime DLLs
  for (const dll of requiredEngineDlls) {
    results.push(checkFile(unpackedDir, 'resources/native/' + dll))
  }

  // 5. Viewport runtime DLLs
  for (const dll of requiredViewportDlls) {
    results.push(checkFile(unpackedDir, 'resources/native/' + dll))
  }

  // 6. PROJ data
  for (const dataFile of requiredProjData) {
    results.push(checkFile(unpackedDir, 'resources/share/proj/' + dataFile))
  }

  // 7. Frontend production files
  results.push(checkFile(unpackedDir, 'resources/frontend/dist/index.html'))
  const frontendAssetsDir = path.join(unpackedDir, 'resources/frontend/dist/assets')
  const hasFrontendAssets = fs.existsSync(frontendAssetsDir) &&
    fs.readdirSync(frontendAssetsDir).some(function (f) { return f.endsWith('.js') })
  results.push({
    name: 'resources/frontend/dist/assets/*.js',
    passed: hasFrontendAssets,
    detail: hasFrontendAssets ? 'found' : 'MISSING',
  })

  // 8. App.asar
  results.push(checkFile(unpackedDir, 'resources/app.asar'))

  // Report
  let allPassed = true
  for (const result of results) {
    const status = result.passed ? 'PASS' : 'FAIL'
    if (!result.passed) {
      allPassed = false
    }
    console.log('  ' + status + '  ' + result.name + ' — ' + result.detail)
  }

  console.log('')
  if (allPassed) {
    console.log('All ' + results.length + ' checks passed. Version target: ' + expectedVersion)
    process.exit(0)
  } else {
    const failed = results.filter(function (r) { return !r.passed }).length
    console.error(failed + ' of ' + results.length + ' checks FAILED.')
    process.exit(1)
  }
}

main()
