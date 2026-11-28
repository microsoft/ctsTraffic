# Collects candidate WER dumps for one endpoint after its workload has ended.

function Invoke-CtsPilotWerCleanup {
    param(
        [Parameter(Mandatory)]$GuestSession,
        [Parameter(Mandatory)]$Before,
        [Parameter(Mandatory)][string]$GuestRunDirectory,
        [Parameter(Mandatory)][string]$HostRunDirectory,
        [Parameter(Mandatory)][ValidateRange(1, 600)][int]$TimeoutSeconds
    )
    Assert-CtsPilotGuardianDirectory -Directory $GuestRunDirectory
    Assert-CtsPilotGuardianDirectory -Directory $HostRunDirectory
    Assert-CtsPilotWerInventory -Inventory $Before
    if ((Get-CtsProp $GuestSession 'Target') -ine $Before.Target) {
        throw 'WER baseline does not match the guest session.'
    }
    if ([IO.Path]::GetFullPath($GuestRunDirectory).TrimEnd('\') -ieq
        [IO.Path]::GetFullPath($Before.Directory).TrimEnd('\')) {
        throw 'WER source and guest run directories must be distinct.'
    }

    $result = [pscustomobject]@{
        Outcome = 'Incomplete'; Before = $Before; After = $null; Comparison = $null
        Copies = [Collections.Generic.List[object]]::new(); Error = $null
    }
    try {
        $result.After = Get-CtsPilotWerInventory -GuestSession $GuestSession -Directory $Before.Directory -TimeoutSeconds $TimeoutSeconds
        $result.Comparison = Compare-CtsPilotWerInventory -Before $Before -After $result.After
    }
    catch {
        $result.Error = "WER final inventory or comparison is unknown: $($_.Exception.Message)"
        return $result
    }

    $candidates = @(
        foreach ($file in $result.Comparison.Candidates) {
            [pscustomobject]@{ Name = $file.Name; Kind = 'NewName' }
        }
        foreach ($file in $result.Comparison.Ambiguous) {
            if ($file.Reason -eq 'Changed') { [pscustomobject]@{ Name = $file.Name; Kind = 'ChangedExisting' } }
        }
    )
    foreach ($candidate in $candidates) {
        $transferToken = [guid]::NewGuid().ToString('N')
        $copy = [pscustomobject]@{
            Name = $candidate.Name; Kind = $candidate.Kind
            GuestPath = [IO.Path]::Combine($GuestRunDirectory, ('wer-' + $candidate.Name))
            HostDirectory = [IO.Path]::Combine($HostRunDirectory, "pilot-$transferToken")
            TransferToken = $transferToken; Outcome = 'NotAttempted'
            GuestReceipt = $null; HostReceipt = $null; Error = $null
        }
        $result.Copies.Add($copy)
        try {
            $receipts = @(Copy-CtsPilotWerCandidate -GuestSession $GuestSession -Before $Before -After $result.After `
                -Name $candidate.Name -RunDirectory $GuestRunDirectory -TimeoutSeconds $TimeoutSeconds)
            if ($receipts.Count -ne 1) { throw 'Guest WER copy did not return one receipt.' }
            $copy.GuestReceipt = $receipts[0]
            $copy.Outcome = 'GuestCopied'
        }
        catch {
            $copy.Outcome = 'GuestCopyUnknown'; $copy.Error = $_.Exception.Message
            continue
        }
        try {
            $receipts = @(Copy-CtsPilotWerToHost -GuestSession $GuestSession -Receipt $copy.GuestReceipt `
                -GuestRunDirectory $GuestRunDirectory -HostRunDirectory $HostRunDirectory -TransferToken $transferToken)
            if ($receipts.Count -ne 1) { throw 'Host WER transfer did not return one receipt.' }
            $copy.HostReceipt = $receipts[0]
            $copy.Outcome = 'Transferred'
        }
        catch { $copy.Outcome = 'HostTransferUnknown'; $copy.Error = $_.Exception.Message }
    }
    if (-not $result.Comparison.BootChanged -and @($result.Comparison.Ambiguous).Count -eq 0 -and
        @($result.Copies | Where-Object Outcome -ne 'Transferred').Count -eq 0) {
        $result.Outcome = 'Completed'
    }
    return $result
}
