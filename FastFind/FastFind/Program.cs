using System.Runtime;

namespace FastFind;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        // 검색 결과 배열처럼 큰 배열을 자주 만들므로, 화면이 멈추는 전체 GC를 되도록 피하게 합니다
        GCSettings.LatencyMode = GCLatencyMode.SustainedLowLatency;

        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}
