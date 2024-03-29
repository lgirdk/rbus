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
    ../include
    )

if (BUILD_RBUS_INTERFACE_SAMPLE_APPS)

add_executable(rbusSampleProvider provider/rbusSampleProvider.c)
add_dependencies(rbusSampleProvider rbus)
target_link_libraries(rbusSampleProvider rbus)

add_executable(rbusSampleConsumer consumer/rbusSampleConsumer.c)
add_dependencies(rbusSampleConsumer rbus)
target_link_libraries(rbusSampleConsumer rbus)

add_executable(rbusTableProvider provider/rbusTableProvider.c provider/utilities.c)
add_dependencies(rbusTableProvider rbus)
target_link_libraries(rbusTableProvider rbus)

add_executable(rbusSampleTableProvider provider/rbusSampleTableProvider.c)
add_dependencies(rbusSampleTableProvider rbus)
target_link_libraries(rbusSampleTableProvider rbus)

add_executable(rbusTableConsumer consumer/rbusTableConsumer.c)
add_dependencies(rbusTableConsumer rbus)
target_link_libraries(rbusTableConsumer rbus)

add_executable(rbusEventProvider provider/rbusEventProvider.c)
add_dependencies(rbusEventProvider rbus)
target_link_libraries(rbusEventProvider rbus)

add_executable(rbusEventConsumer consumer/rbusEventConsumer.c)
add_dependencies(rbusEventConsumer rbus)
target_link_libraries(rbusEventConsumer rbus)

add_executable(rbusGeneralEventProvider provider/rbusGeneralEventProvider.c)
add_dependencies(rbusGeneralEventProvider rbus)
target_link_libraries(rbusGeneralEventProvider rbus)

add_executable(rbusGeneralEventConsumer consumer/rbusGeneralEventConsumer.c)
add_dependencies(rbusGeneralEventConsumer rbus)
target_link_libraries(rbusGeneralEventConsumer rbus)

add_executable(rbusValueChangeProvider provider/rbusValueChangeProvider.c)
add_dependencies(rbusValueChangeProvider rbus)
target_link_libraries(rbusValueChangeProvider rbus)

add_executable(rbusValueChangeConsumer consumer/rbusValueChangeConsumer.c)
add_dependencies(rbusValueChangeConsumer rbus)
target_link_libraries(rbusValueChangeConsumer rbus)

add_executable(rbusMethodProvider provider/rbusMethodProvider.c)
add_dependencies(rbusMethodProvider rbus)
target_link_libraries(rbusMethodProvider rbus)

add_executable(rbusMethodConsumer consumer/rbusMethodConsumer.c)
add_dependencies(rbusMethodConsumer rbus)
target_link_libraries(rbusMethodConsumer rbus)

if (BUILD_FOR_DESKTOP)
add_executable(rbusSubConsumer consumer/rbusSubConsumer.c)
add_dependencies(rbusSubConsumer rbus)
target_link_libraries(rbusSubConsumer rbus)

add_executable(rbusEvProvider provider/rbusEvProvider.c)
add_dependencies(rbusEvProvider rbus)
target_link_libraries(rbusEvProvider rbus)
endif (BUILD_FOR_DESKTOP)

add_executable(rbusMessageListener message/rbusMessageListener.c)
add_dependencies(rbusMessageListener rbus)
target_link_libraries(rbusMessageListener rbus)

add_executable(rbusMessageSender message/rbusMessageSender.c)
add_dependencies(rbusMessageSender rbus)
target_link_libraries(rbusMessageSender rbus)

add_executable(rbusTR104MtaAgentApp provider/mta_proxy_tr104.c
provider/mta_hal_tr104.c provider/mta_hal_tr104.h)
add_dependencies(rbusTR104MtaAgentApp rbus)
target_link_libraries(rbusTR104MtaAgentApp rbus)

install (TARGETS 
    rbusSampleProvider
    rbusSampleConsumer
    rbusTableProvider
    rbusSampleTableProvider
    rbusTableConsumer
    rbusEventProvider
    rbusEventConsumer
    rbusMethodProvider
    rbusMethodConsumer
    rbusGeneralEventProvider
    rbusGeneralEventConsumer
    rbusValueChangeProvider
    rbusValueChangeConsumer
    rbusMessageListener
    rbusMessageSender
    RUNTIME DESTINATION bin)

endif (BUILD_RBUS_INTERFACE_SAMPLE_APPS)

