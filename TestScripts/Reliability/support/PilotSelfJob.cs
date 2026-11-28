using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace CtsReliability
{
    // Load only in a dedicated guardian process: this permanently confines the caller.
    public static class PilotSelfJob
    {
        private const uint KillOnJobClose = 0x00002000;
        private const int ExtendedLimitInformation = 9;
        private static readonly object Sync = new object();
        private static SafeFileHandle job;

        [StructLayout(LayoutKind.Sequential)]
        private struct BasicLimitInformation
        {
            public long PerProcessUserTimeLimit;
            public long PerJobUserTimeLimit;
            public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize;
            public UIntPtr MaximumWorkingSetSize;
            public uint ActiveProcessLimit;
            public UIntPtr Affinity;
            public uint PriorityClass;
            public uint SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct IoCounters
        {
            public ulong ReadOperationCount, WriteOperationCount, OtherOperationCount;
            public ulong ReadTransferCount, WriteTransferCount, OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ExtendedLimits
        {
            public BasicLimitInformation BasicLimitInformation;
            public IoCounters IoInfo;
            public UIntPtr ProcessMemoryLimit, JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed, PeakJobMemoryUsed;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateJobObjectW(IntPtr attributes, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetInformationJobObject(
            SafeFileHandle handle, int informationClass, ref ExtendedLimits information, uint length);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool AssignProcessToJobObject(SafeFileHandle handle, IntPtr process);

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetCurrentProcess();

        public static void Enter()
        {
            lock (Sync)
            {
                if (job != null) throw new InvalidOperationException("The guardian is already confined.");

                SafeFileHandle candidate = CreateJobObjectW(IntPtr.Zero, null);
                if (candidate == null || candidate.IsInvalid)
                {
                    int error = Marshal.GetLastWin32Error();
                    if (candidate != null) candidate.Dispose();
                    throw new Win32Exception(error, "Could not create guardian job.");
                }

                bool pinned = false;
                bool assigned = false;
                try
                {
                    var limits = new ExtendedLimits();
                    limits.BasicLimitInformation.LimitFlags = KillOnJobClose;
                    if (!SetInformationJobObject(candidate, ExtendedLimitInformation, ref limits, (uint)Marshal.SizeOf(typeof(ExtendedLimits))))
                        throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not configure guardian job.");
                    // Closing the last handle kills the guardian and its children.
                    candidate.DangerousAddRef(ref pinned);
                    if (!pinned) throw new InvalidOperationException("Could not retain guardian job handle.");
                    if (!AssignProcessToJobObject(candidate, GetCurrentProcess()))
                        throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not confine guardian process.");
                    assigned = true;
                    job = candidate;
                }
                finally
                {
                    if (!assigned)
                    {
                        if (pinned) candidate.DangerousRelease();
                        candidate.Dispose();
                    }
                }
            }
        }
    }
}
