idf_component_register(SRCS "src/wifi_ap.c"
                            "src/http_server.c"
                            "src/bt_manager.c"
                            "main.c"
                       INCLUDE_DIRS "include"
                       EMBED_FILES "web/index.html"
                                   "web/style.css"
                                   "web/app.js")