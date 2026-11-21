$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

$configureArgs = @("-S", ".", "-B", "build", "-DBUILD_TESTING=ON")
if ($env:VCPKG_ROOT) {
    $toolchain = Join-Path $env:VCPKG_ROOT "scripts/buildsystems/vcpkg.cmake"
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build build --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir build --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
