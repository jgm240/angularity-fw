idf_component_register(SRCS "computer_mode_main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_gpio esp_driver_uart
                       )
