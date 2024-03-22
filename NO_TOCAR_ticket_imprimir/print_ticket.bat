@echo off

echo Start printing Excel file...

REM Launch the Windows Script Host
cscript //nologo //B "%~F0?.wsf"
exit /b

<job id="PrintJob">
   <script language="VBScript">
   Set objExcel = CreateObject("Excel.Application")
   objExcel.Visible = False

   Set objWorkbook = objExcel.Workbooks.Open("C:\Users\rocio\work\tintoreria\NO_TOCAR_ticket_imprimir\ImprimirTicket.xlsx")

   objWorkbook.PrintOut
   objWorkbook.Close False
   objExcel.Quit
   </script>
</job>
