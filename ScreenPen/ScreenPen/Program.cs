namespace ScreenPen;

internal static class Program
{
    /// <summary>
    /// 프로그램 시작 지점.
    /// 실행 인자에 /standby 를 주면 판서 화면 대신 대기 버튼만 띄운 상태로 시작합니다.
    /// </summary>
    [STAThread]
    private static void Main(string[] args)
    {
        // 중복 실행 방지
        using var mutex = new Mutex(true, "ScreenPen_SingleInstance_Mutex", out bool isFirstInstance);
        if (!isFirstInstance)
        {
            MessageBox.Show("화면 판서 프로그램이 이미 실행 중입니다.\n화면의 판서 버튼이나 트레이 아이콘을 사용하세요.",
                "화면 판서", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        ApplicationConfiguration.Initialize();

        Application.ThreadException += (_, e) =>
            MessageBox.Show("오류가 발생했습니다.\n\n" + e.Exception.Message, "화면 판서",
                MessageBoxButtons.OK, MessageBoxIcon.Error);

        bool startInStandby = args.Any(a =>
            a.Equals("/standby", StringComparison.OrdinalIgnoreCase) ||
            a.Equals("--standby", StringComparison.OrdinalIgnoreCase));

        Application.Run(new AppController(startInStandby));
    }
}
