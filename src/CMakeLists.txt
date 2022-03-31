#############################################################################
# If not stated otherwise in this file or this component's Licenses.txt file
# the following copyright and licenses apply:
#
# Copyright 2019 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#############################################################################


include_directories(
    ../include)
add_library(
    rbus
    SHARED
    rbus.c
    rbus_handle.c
    rbus_message.c
    rbus_value.c
    rbus_property.c
    rbus_object.c
    rbus_buffer.c
    rbus_filter.c
    rbus_element.c
    rbus_valuechange.c
    rbus_subscriptions.c
    rbus_tokenchain.c
    rbus_asyncsubscribe.c
    rbus_config.c)

target_link_libraries(
    rbus
    ${RTMESSAGE_LIBRARIES}
    ${RBUSCORE_LIBRARIES}
    -fPIC
    -pthread)

target_include_directories (rbus PUBLIC ${RBUSCORE_INCLUDE_DIRS})
target_include_directories (rbus PUBLIC ${RTMESSAGE_INCLUDE_DIRS})
target_include_directories (rbus PUBLIC ${RDKLOGGER_INCLUDE_DIRS})

set_target_properties(rbus
    PROPERTIES SOVERSION "0"
    VERSION "${PROJECT_VERSION}")

install (TARGETS rbus
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib)

install (DIRECTORY ../include/
    DESTINATION "include/rbus")

