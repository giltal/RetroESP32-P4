# prboom is compiled as a PSRAM app (papp) via tools/build_doom_papp.ps1,
# NOT as a regular ESP-IDF component.  Register as empty so the build
# system doesn't fail when this directory is picked up by EXTRA_COMPONENT_DIRS.
idf_component_register()
