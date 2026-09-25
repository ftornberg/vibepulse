# TID: klocka, pomodoro och timer. Kärnan (time_core) och presentatören
# (time_present) är ren C och hosttestas; time_views/app_time är LVGL.
# Opt-in: dras bara in av main när bygget sätter TORGET_WITH_TIME (2.16).
idf_component_register(
  SRCS "time_core.c" "time_present.c" "time_views.c" "app_time.c"
  INCLUDE_DIRS "."
  # Ingen egen idf_component.yml: ett manifest ändrar dependencies.lock:s
  # manifest_hash och får varje bygge att lösa om låset. LVGL finns redan i grafen.
  REQUIRES torget_app lvgl__lvgl)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
