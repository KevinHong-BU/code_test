using System;
using System.Windows.Forms;
using BinaryCalculator.UI;

namespace BinaryCalculator
{
    internal static class Program
    {
        /// <summary>
        /// 프로그램 시작점
        /// </summary>
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
