#Requires -Modules @{ ModuleName = 'Pester'; ModuleVersion = '5.0' }

BeforeAll {
    Import-Module (Join-Path $PSScriptRoot '..\CtsReliability.psm1') -Force
    $script:Launch = [pscustomobject]@{
        Token = ('a' * 32); ProcessId = 123
        HeartbeatPath = 'C:\runs\pilot\heartbeat.txt'
        StdoutPath = 'C:\runs\pilot\g1b-pilot-server-guardian-process-stdout.txt'
        StderrPath = 'C:\runs\pilot\g1b-pilot-server-guardian-process-stderr.txt'
    }
    $script:CompletedJob = Start-Job { }
    $script:FailedJob = Start-Job { throw 'fixture failure' }
    $null = Wait-Job -Job @($script:CompletedJob, $script:FailedJob)
}

AfterAll {
    Remove-Job -Job @($script:CompletedJob, $script:FailedJob) -Force
}

Describe 'worker event observation' {
    BeforeEach {
        Mock -ModuleName CtsReliability Receive-Job { }
        Mock -ModuleName CtsReliability Stop-Job { throw 'Unexpected stop' }
    }

    It 'retains ordered events and a nonzero child completion' {
        InModuleScope CtsReliability -Parameters @{ Launch = $script:Launch } {
            param($Launch)
            $observation = New-CtsPilotWorkerObservation -Job ([pscustomobject]@{ State = 'Running' }) `
                -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
            Add-CtsPilotWorkerObservationEvent $observation ([pscustomobject]@{ Event = 'Launched'; Role = 'Server'; Launch = $Launch })
            Add-CtsPilotWorkerObservationEvent $observation ([pscustomobject]@{ Event = 'Startup'; Role = 'Server'; Result = @{ Outcome = 'Listening' } })
            Add-CtsPilotWorkerObservationEvent $observation ([pscustomobject]@{
                Event = 'Finished'; Role = 'Server'; Result = @{ Outcome = 'Completed'; Evidence = @{ ChildExitCode = 7 } }
            })
            $observation.State | Should -Be 'Finished'
            $observation.Result.Evidence.ChildExitCode | Should -Be 7
            $observation.Events.Count | Should -Be 3
        }
    }

    It 'drains output again after the job is observed completed' {
        $script:ReceiveCount = 0
        Mock -ModuleName CtsReliability Receive-Job {
            $script:ReceiveCount++
            if ($script:ReceiveCount -eq 1) {
                [pscustomobject]@{ Event = 'Launched'; Role = 'Server'; Launch = $script:Launch }
                [pscustomobject]@{ Event = 'Startup'; Role = 'Server'; Result = @{ Outcome = 'ListenerTimeout' } }
            }
            else {
                [pscustomobject]@{ Event = 'Finished'; Role = 'Server'; Result = @{ Outcome = 'Completed' } }
            }
        }
        InModuleScope CtsReliability -Parameters @{ Job = $script:CompletedJob } {
            param($Job)
            $observation = New-CtsPilotWorkerObservation -Job $Job `
                -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
            Update-CtsPilotWorkerObservation $observation
            $observation.State | Should -Be 'Finished'
            $observation.Startup.Outcome | Should -Be 'ListenerTimeout'
            $observation.Events.Count | Should -Be 3
        }
        Should -Invoke -ModuleName CtsReliability Receive-Job -Times 2 -Exactly
    }

    It 'marks missing completion or failed jobs unknown, retaining events' {
        InModuleScope CtsReliability -Parameters @{ Launch = $script:Launch; CompletedJob = $script:CompletedJob; FailedJob = $script:FailedJob } {
            param($Launch, $CompletedJob, $FailedJob)
            $observation = New-CtsPilotWorkerObservation -Job $CompletedJob `
                -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
            Add-CtsPilotWorkerObservationEvent $observation ([pscustomobject]@{ Event = 'Launched'; Role = 'Server'; Launch = $Launch })
            Update-CtsPilotWorkerObservation $observation
            $observation.State | Should -Be 'Unknown'
            $observation.Events.Count | Should -Be 1
            $observation.Error | Should -Match 'without reliable completion'

            $finished = New-CtsPilotWorkerObservation -Job $FailedJob `
                -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
            Add-CtsPilotWorkerObservationEvent $finished ([pscustomobject]@{ Event = 'Launched'; Role = 'Server'; Launch = $Launch })
            Add-CtsPilotWorkerObservationEvent $finished ([pscustomobject]@{ Event = 'Startup'; Role = 'Server'; Result = @{ Outcome = 'Listening' } })
            Add-CtsPilotWorkerObservationEvent $finished ([pscustomobject]@{ Event = 'Finished'; Role = 'Server'; Result = @{ Outcome = 'Completed' } })
            Update-CtsPilotWorkerObservation $finished
            $finished.State | Should -Be 'Unknown'
            $finished.Result.Outcome | Should -Be 'Completed'
        }
    }

    It 'rejects mismatched and out-of-order events without stopping a peer' {
        InModuleScope CtsReliability -Parameters @{ Launch = $script:Launch } {
            param($Launch)
            $badRole = New-CtsPilotWorkerObservation -Job ([pscustomobject]@{ State = 'Running' }) `
                -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
            Add-CtsPilotWorkerObservationEvent $badRole ([pscustomobject]@{ Event = 'Launched'; Role = 'Client'; Launch = $Launch })
            $badRole.State | Should -Be 'Unknown'

            $badToken = New-CtsPilotWorkerObservation -Job ([pscustomobject]@{ State = 'Running' }) `
                -Role Server -Directory 'C:\runs\pilot' -Token ('b' * 32)
            Add-CtsPilotWorkerObservationEvent $badToken ([pscustomobject]@{ Event = 'Launched'; Role = 'Server'; Launch = $Launch })
            $badToken.State | Should -Be 'Unknown'

            $outOfOrder = New-CtsPilotWorkerObservation -Job ([pscustomobject]@{ State = 'Running' }) `
                -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
            Add-CtsPilotWorkerObservationEvent $outOfOrder ([pscustomobject]@{ Event = 'Finished'; Role = 'Server'; Result = @{ Outcome = 'Completed' } })
            $outOfOrder.State | Should -Be 'Unknown'
            $outOfOrder.Events.Count | Should -Be 1
        }
        Should -Invoke -ModuleName CtsReliability Stop-Job -Times 0
    }

    It 'keeps one worker unknown when collection fails without changing the other' {
        Mock -ModuleName CtsReliability Receive-Job { throw 'lost stream' }
        InModuleScope CtsReliability -Parameters @{ Job = $script:CompletedJob } {
            param($Job)
            $first = New-CtsPilotWorkerObservation -Job $Job `
                -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
            $peer = New-CtsPilotWorkerObservation -Job ([pscustomobject]@{ State = 'Running' }) `
                -Role Client -Directory 'C:\runs\peer' -Token ('b' * 32)
            Update-CtsPilotWorkerObservation $first
            $first.State | Should -Be 'Unknown'
            $first.Error | Should -Match 'lost stream'
            $peer.State | Should -Be 'Starting'
        }
        Should -Invoke -ModuleName CtsReliability Stop-Job -Times 0
    }
}

Describe 'worker event collection from a real job' {
    It 'marks an error from a still-running job unknown while retaining its output' {
        $job = Start-Job -ArgumentList $script:Launch -ScriptBlock {
            param($launch)
            [pscustomobject]@{ Event = 'Launched'; Role = 'Server'; Launch = $launch }
            Write-Error 'fixture stream error'
            Start-Sleep -Seconds 10
        }
        try {
            $clock = [Diagnostics.Stopwatch]::StartNew()
            while ($job.ChildJobs[0].Error.Count -eq 0 -and
                $job.State -in @('Running', 'NotStarted') -and $clock.Elapsed.TotalSeconds -lt 15) {
                Start-Sleep -Milliseconds 100
            }
            $job.State | Should -Be 'Running'
            InModuleScope CtsReliability -Parameters @{ Job = $job } {
                param($Job)
                $observation = New-CtsPilotWorkerObservation -Job $Job `
                    -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
                Update-CtsPilotWorkerObservation $observation
                $observation.State | Should -Be 'Unknown'
                $observation.Error | Should -Match 'fixture stream error'
                $observation.Launch.ProcessId | Should -Be 123
            }
        }
        finally {
            if ($job.State -eq 'Running') { Stop-Job -Job $job }
            Remove-Job -Job $job -Force
        }
    }

    It 'retains output emitted before a job error' {
        $job = Start-Job -ArgumentList $script:Launch -ScriptBlock {
            param($launch)
            [pscustomobject]@{ Event = 'Launched'; Role = 'Server'; Launch = $launch }
            throw 'fixture worker failure'
        }
        try {
            $null = Wait-Job -Job $job
            InModuleScope CtsReliability -Parameters @{ Job = $job } {
                param($Job)
                $observation = New-CtsPilotWorkerObservation -Job $Job `
                    -Role Server -Directory 'C:\runs\pilot' -Token ('a' * 32)
                Update-CtsPilotWorkerObservation $observation
                $observation.State | Should -Be 'Unknown'
                $observation.Events.Count | Should -Be 1
                $observation.Launch.ProcessId | Should -Be 123
            }
        }
        finally { Remove-Job -Job $job -Force }
    }
}
