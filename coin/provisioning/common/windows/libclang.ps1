param([Int32]$archVer=32)
. "$PSScriptRoot\helpers.ps1"

# PySide versions following 5.6 use a C++ parser based on Clang (http://clang.org/).
# The Clang library (C-bindings), version 3.9 or higher is required for building.

Get-Content "$PSScriptRoot\..\shared\sw_versions.txt" | Foreach-Object {
    $var = $_.Split('=')
    New-Variable -Name $var[0] -Value $var[1]
    $libclang_version = $libclang_version -replace '["."]'
}

if ( $archVer -eq 64 ) {
    $sha1 = "dc42beb0efff130c4d7dfef3c97adf26f1ab04e0"
    $url = "http://ci-files01-hki.intra.qt.io/input/libclang/qt/libclang-release_$libclang_version-windows-vs2015_64.7z"
} else {
    $sha1 = "64e826c00ae632fbb28655e6e1fa9194980e1205"
    $url = "http://ci-files01-hki.intra.qt.io/input/libclang/qt/libclang-release_$libclang_version-windows-vs2015_32.7z"
}

$zip = "c:\users\qt\downloads\libclang.7z"
$destination = "C:\Utils\libclang-" + $libclang_version

Download $url $url $zip
Verify-Checksum $zip $sha1

Extract-7Zip $zip C:\Utils\
Rename-Item C:\Utils\libclang $destination
Remove-Item -Force -Path $zip

Set-EnvironmentVariable "LLVM_INSTALL_DIR" $destination

Write-Output "libClang = $libclang_version" >> ~/versions.txt
