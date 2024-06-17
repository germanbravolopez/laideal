# La Ideal
La Ideal laundry software app

## Deploy application
- [windeployqt](https://medium.com/swlh/how-to-deploy-your-qt-cross-platform-applications-to-windows-operating-system-by-using-windeployqt-a7cd5663d46e)
- [Tutorial to create installer](https://www.youtube.com/watch?v=Y9Ovo2XJHDs)
- Application to create installer: [Inno Setup](https://jrsoftware.org/isinfo.php)

## Release procedure:

1. Update release number in `laideal/CMakeLists.txt`
2. Update release notes
3. Run application in Qt with "*Release*" option
4. Close Qt
5. Open a Qt bash with admin rights
6. `cd C:\Users\gebra\work\tintoreria\laideal\releases`
7. `deploy_laideal_run_in_qt_cmd.bat`
8. Set number of release, for example: "r4.2"
10. Change icon after installed