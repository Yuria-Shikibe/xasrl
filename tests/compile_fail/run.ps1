param([Parameter(Mandatory = $true)][string]$Compiler)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$source = Join-Path $PSScriptRoot 'metadata.cpp'
$cases = @(
    @{ Id = 0; Tokens = @() },
    @{ Id = 1; Tokens = @('duplicate_field_id', 'duplicate::second', 'field id=7', 'duplicate::first') },
    @{ Id = 2; Tokens = @('conflicting_annotations', 'bad_flags', 'mask=1', 'bits=2') },
    @{ Id = 3; Tokens = @('unsupported_entity', 'outer::child', 'inner::pointer') },
    @{ Id = 4; Tokens = @('extent_overflow', 'huge', 'maximum_entry_size') },
    @{ Id = 5; Tokens = @('invalid_object_representation', 'untagged', 'tagged codec') },
    @{ Id = 6; Tokens = @('wrapped_object::child', 'enum_object::value', 'bad_flags', 'mask=1') },
    @{ Id = 7; Tokens = @('duplicate_field_id', 'field id=7') },
    @{ Id = 8; Tokens = @('enum_object', 'enum_object::value', 'bad_flags', 'mask=1') },
    @{ Id = 9; Tokens = @('fixed size out of range', 'left=', 'maximum_entry_size=') },
    @{ Id = 10; Tokens = @('addition overflow', 'right=1', 'maximum_entry_size=') },
    @{ Id = 11; Tokens = @('multiplication overflow', 'right=2', 'maximum_entry_size=') },
    @{ Id = 12; Tokens = @('extent_overflow', 'overflow_container::items', 'multiplication overflow', 'right=2') },
    @{ Id = 13; Tokens = @('conflicting_annotations', 'duplicate_validation::value') },
    @{ Id = 14; Tokens = @('invalid_object_representation', 'unchecked_union', 'tagged codec') },
    @{ Id = 15; Tokens = @('unchecked_bad_flags', 'bad_flags', 'mask=1') },
    @{ Id = 16; Tokens = @('[invalid_object_representation] -> float_record -> float_record::child -> float_leaf', 'std::has_unique_object_representations_v<T>', '.allow_non_unique_presentation = true') }
)

foreach ($case in $cases) {
    $output = (& $Compiler '-std=c++26' '-freflection' '-fsyntax-only' '-fdiagnostics-color=never' "-I$repoRoot/src" "-DSRL_FAILURE_CASE=$($case.Id)" $source 2>&1 | Out-String)
    $compilerExit = $LASTEXITCODE
    if ($case.Id -eq 0) {
        if ($compilerExit -ne 0) { throw "Positive control failed:`n$output" }
    } else {
        if ($compilerExit -eq 0) { throw "Case $($case.Id) unexpectedly compiled" }
        if (!$output.Contains('uncaught exception') -or !$output.Contains("'what()'")) {
            throw "Case $($case.Id) did not report the schema exception:`n$output"
        }
        foreach ($token in $case.Tokens) {
            if (!$output.Contains($token)) { throw "Case $($case.Id) missing '$token':`n$output" }
        }
    }
    Write-Output "PASS metadata case $($case.Id)"
}
