using System.Collections.Concurrent;
using System.Diagnostics;
using static FastFind.NativeMethods;

namespace FastFind;

/// <summary>
/// MFT를 읽을 수 없을 때(관리자 권한 없음, FAT/exFAT/ReFS 등) 쓰는 대체 색인 방법.
/// 폴더 여러 개를 CPU 코어 수만큼 동시에 읽고, FindFirstFileEx의
/// 빠른 옵션(FindExInfoBasic + LARGE_FETCH)으로 시스템 호출 횟수를 줄입니다.
/// </summary>
internal static class DirectoryScanner
{
    private const int FindExInfoBasic = 1;

    public static VolumeIndex Build(string root, CancellationToken ct)
    {
        var sw = Stopwatch.StartNew();
        var index = new VolumeIndex(root);
        var work = new ConcurrentStack<(string Path, int Dir)>();
        work.Push((root, -1));
        int pending = 1;
        var gate = new object();

        void Worker()
        {
            var children = new List<(string Name, uint Attr)>(256);
            var spin = new SpinWait();
            while (!ct.IsCancellationRequested)
            {
                if (!work.TryPop(out var item))
                {
                    if (Volatile.Read(ref pending) == 0) return;
                    spin.SpinOnce();
                    continue;
                }
                spin.Reset();

                children.Clear();
                ReadDirectory(item.Path, children);
                if (children.Count > 0)
                {
                    var subdirs = new List<(string, int)>();
                    lock (gate)
                    {
                        foreach (var (name, attr) in children)
                        {
                            int idx = index.Add(name, item.Dir, attr);
                            // 정션/심볼릭 링크 폴더는 따라가지 않음 (무한 반복, 중복 방지)
                            if ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attr & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
                                subdirs.Add((item.Path + name + "\\", idx));
                        }
                    }
                    if (subdirs.Count > 0)
                    {
                        Interlocked.Add(ref pending, subdirs.Count);
                        work.PushRange(subdirs.ToArray());
                    }
                }
                Interlocked.Decrement(ref pending);
            }
        }

        int n = Math.Clamp(Environment.ProcessorCount, 2, 16);
        var threads = new Thread[n];
        for (int i = 0; i < n; i++)
        {
            threads[i] = new Thread(Worker) { IsBackground = true, Name = "Scan " + root };
            threads[i].Start();
        }
        foreach (var t in threads) t.Join();

        index.Method = "폴더 탐색";
        index.BuildTime = sw.Elapsed;
        return index;
    }

    private static void ReadDirectory(string dir, List<(string, uint)> into)
    {
        string pattern = (dir.Length >= 240 ? @"\\?\" + dir : dir) + "*";
        IntPtr h = FindFirstFileExW(pattern, FindExInfoBasic, out var fd, 0, IntPtr.Zero, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return;
        try
        {
            do
            {
                string name = fd.cFileName;
                if (name == "." || name == "..") continue;
                into.Add((name, fd.dwFileAttributes));
            } while (FindNextFileW(h, out fd));
        }
        finally { FindClose(h); }
    }
}
