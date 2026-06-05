cmake_minimum_required(VERSION 3.22)

set(BT_COMMON_DIR "$ENV{IDF_PATH}/examples/bluetooth/bluedroid/classic_bt/common")

set(EXTRA_COMPONENT_DIRS 
    "components"
    "${BT_COMMON_DIR}/bt_app_core_utils"
    "${BT_COMMON_DIR}/bredr_app_common_utils"
    "${BT_COMMON_DIR}/a2dp_utils/a2dp_sink_common_utils"
    "${BT_COMMON_DIR}/a2dp_utils/a2dp_sink_int_codec_utils"
    "${BT_COMMON_DIR}/a2dp_utils/a2dp_sink_ext_codec_utils"
    "${BT_COMMON_DIR}/avrcp_utils/avrcp_common_utils"
    "${BT_COMMON_DIR}/avrcp_utils/avrcp_metadata_utils"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

project(especiallyAlfa)