# Inno Setup is a build tool, not a viewer runtime dependency.
include_guard()
set(_inno_default OFF)
if (WINDOWS AND PACKAGE)
  set(_inno_default ON)
endif ()
option(USE_INNOSETUP "Package Windows installers with Inno Setup" ${_inno_default})
