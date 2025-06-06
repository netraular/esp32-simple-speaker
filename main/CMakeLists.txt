idf_component_register(SRCS "main.cpp" "./controllers/screen_manager/screen_manager.cpp" "./controllers/button_manager/button_manager.cpp" 
                   INCLUDE_DIRS "."
                   )

idf_build_set_property(PARTITION_TABLE_FILENAME partitions.csv)