set(krunnerintegration_SOURCES
    main.cpp
    windowsrunnerinterface.cpp
)

qt5_add_dbus_adaptor(krunnerintegration_SOURCES org.kde.krunner1.xml windowsrunnerinterface.h KWin::WindowsRunner)

add_library(krunnerintegration MODULE ${krunnerintegration_SOURCES})
target_link_libraries(krunnerintegration kwin KF5::Runner)

install(TARGETS krunnerintegration DESTINATION ${PLUGIN_INSTALL_DIR}/kwin/plugins/)
install(FILES kwin-runner-windows.desktop DESTINATION ${KDE_INSTALL_DATAROOTDIR}/krunner/dbusplugins)
