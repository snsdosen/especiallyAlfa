idf_component_register(SRCS "bt_player.c"
                    INCLUDE_DIRS "include"
                    REQUIRES buscom bt bt_app_core_utils avrcp_common_utils avrcp_metadata_utils bredr_app_common_utils a2dp_sink_common_utils esp_driver_gpio esp_driver_i2s a2dp_sink_int_codec_utils)