/*
obs-websocket
Copyright (C) 2016-2017	Stéphane Lepin <stephane.lepin@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <QMainWindow>
#include <QDir>
#include <QUrl>
#include <obs-frontend-api.h>
#include <obs.hpp>
#include "obs-websocket.h"

#include "Utils.h"
#include "Config.h"

Q_DECLARE_METATYPE(OBSScene);

const char* qstring_data_copy(QString value) {
    QByteArray stringData = value.toUtf8();
    const char* constStringData = new const char[stringData.size()]();
    memcpy((void*)constStringData, stringData.constData(), stringData.size());
    return constStringData;
}

obs_data_array_t* stringListToArray(char** strings, char* key) {
    if (!strings)
        return obs_data_array_create();

    obs_data_array_t* list = obs_data_array_create();

    char* value = "";
    for (int i = 0; value != nullptr; i++) {
        value = strings[i];

        OBSDataAutoRelease item = obs_data_create();
        obs_data_set_string(item, key, value);

        if (value)
            obs_data_array_push_back(list, item);
    }

    return list;
}

obs_data_array_t* Utils::GetSceneItems(obs_source_t* source) {
    obs_data_array_t* items = obs_data_array_create();
    OBSScene scene = obs_scene_from_source(source);

    if (!scene)
        return nullptr;

    obs_scene_enum_items(scene, [](
            obs_scene_t* scene,
            obs_sceneitem_t* currentItem,
            void* param)
    {
        obs_data_array_t* data = static_cast<obs_data_array_t*>(param);

        OBSDataAutoRelease itemData = GetSceneItemData(currentItem);
        obs_data_array_insert(data, 0, itemData);
        return true;
    }, items);

    return items;
}

obs_data_t* Utils::GetSceneItemData(obs_sceneitem_t* item) {
    if (!item)
        return nullptr;

    vec2 pos;
    obs_sceneitem_get_pos(item, &pos);

    vec2 scale;
    obs_sceneitem_get_scale(item, &scale);

    // obs_sceneitem_get_source doesn't increase the refcount
    OBSSource itemSource = obs_sceneitem_get_source(item);
    float item_width = float(obs_source_get_width(itemSource));
    float item_height = float(obs_source_get_height(itemSource));

    obs_data_t* data = obs_data_create();
    obs_data_set_string(data, "name",
        obs_source_get_name(obs_sceneitem_get_source(item)));
    obs_data_set_string(data, "type",
        obs_source_get_id(obs_sceneitem_get_source(item)));
    obs_data_set_double(data, "volume",
        obs_source_get_volume(obs_sceneitem_get_source(item)));
    obs_data_set_double(data, "x", pos.x);
    obs_data_set_double(data, "y", pos.y);
    obs_data_set_int(data, "source_cx", (int)item_width);
    obs_data_set_int(data, "source_cy", (int)item_height);
    obs_data_set_double(data, "cx", item_width*  scale.x);
    obs_data_set_double(data, "cy", item_height*  scale.y);
    obs_data_set_bool(data, "render", obs_sceneitem_visible(item));

    return data;
}

obs_sceneitem_t* Utils::GetSceneItemFromName(obs_source_t* source, QString name) {
    struct current_search {
        QString query;
        obs_sceneitem_t* result;
    };

    current_search search;
    search.query = name;
    search.result = nullptr;

    OBSScene scene = obs_scene_from_source(source);
    if (!scene)
        return nullptr;

    obs_scene_enum_items(scene, [](
            obs_scene_t* scene,
            obs_sceneitem_t* currentItem,
            void* param)
    {
        current_search* search = static_cast<current_search*>(param);

        QString currentItemName =
            obs_source_get_name(obs_sceneitem_get_source(currentItem));

        if (currentItemName == search->query) {
            search->result = currentItem;
            obs_sceneitem_addref(search->result);
            return false;
        }

        return true;
    }, &search);

    return search.result;
}

obs_source_t* Utils::GetTransitionFromName(QString searchName) {
    OBSSource foundTransition = nullptr;

    obs_frontend_source_list transition_list = {};
    obs_frontend_get_transitions(&transition_list);

    for (size_t i = 0; i < transition_list.sources.num; i++) {
        obs_source_t* transition = transition_list.sources.array[i];
        QString transitionName = obs_source_get_name(transition);

        if (transitionName == searchName) {
            foundTransition = transition;
            obs_source_addref(foundTransition);
            break;
        }
    }

    obs_frontend_source_list_free(&transition_list);
    return foundTransition;
}

obs_source_t* Utils::GetSceneFromNameOrCurrent(QString sceneName) {
    // Both obs_frontend_get_current_scene() and obs_get_source_by_name()
    // do addref on the return source, so no need to use an OBSSource helper
    obs_source_t* scene = nullptr;

    if (sceneName.isEmpty() || sceneName.isNull())
        scene = obs_frontend_get_current_scene();
    else
        scene = obs_get_source_by_name(sceneName.toUtf8());

    return scene;
}

obs_data_array_t* Utils::GetScenes() {
    obs_frontend_source_list sceneList = {};
    obs_frontend_get_scenes(&sceneList);

    obs_data_array_t* scenes = obs_data_array_create();
    for (size_t i = 0; i < sceneList.sources.num; i++) {
        obs_source_t* scene = sceneList.sources.array[i];
        OBSDataAutoRelease sceneData = GetSceneData(scene);
        obs_data_array_push_back(scenes, sceneData);
    }

    obs_frontend_source_list_free(&sceneList);
    return scenes;
}

obs_data_t* Utils::GetSceneData(obs_source_t* source) {
    OBSDataArrayAutoRelease sceneItems = GetSceneItems(source);

    obs_data_t* sceneData = obs_data_create();
    obs_data_set_string(sceneData, "name", obs_source_get_name(source));
    obs_data_set_array(sceneData, "sources", sceneItems);

    return sceneData;
}

obs_data_array_t* Utils::GetSceneCollections() {
    char** sceneCollections = obs_frontend_get_scene_collections();
    obs_data_array_t* list = stringListToArray(sceneCollections, "sc-name");

    bfree(sceneCollections);
    return list;
}

obs_data_array_t* Utils::GetProfiles() {
    char** profiles = obs_frontend_get_profiles();
    obs_data_array_t* list = stringListToArray(profiles, "profile-name");

    bfree(profiles);
    return list;
}

QSpinBox* Utils::GetTransitionDurationControl() {
    QMainWindow* window = (QMainWindow*)obs_frontend_get_main_window();
    return window->findChild<QSpinBox*>("transitionDuration");
}

int Utils::GetTransitionDuration() {
    QSpinBox* control = GetTransitionDurationControl();
    if (control)
        return control->value();
    else
        return -1;
}

void Utils::SetTransitionDuration(int ms) {
    QSpinBox* control = GetTransitionDurationControl();
    if (control && ms >= 0)
        control->setValue(ms);
}

bool Utils::SetTransitionByName(QString transitionName) {
    OBSSourceAutoRelease transition = GetTransitionFromName(transitionName);

    if (transition) {
        obs_frontend_set_current_transition(transition);
        return true;
    } else {
        return false;
    }
}

QPushButton* Utils::GetPreviewModeButtonControl() {
    QMainWindow* main = (QMainWindow*)obs_frontend_get_main_window();
    return main->findChild<QPushButton*>("modeSwitch");
}

QListWidget* Utils::GetSceneListControl() {
    QMainWindow* main = (QMainWindow*)obs_frontend_get_main_window();
    return main->findChild<QListWidget*>("scenes");
}

obs_scene_t* Utils::SceneListItemToScene(QListWidgetItem* item) {
    if (!item)
        return nullptr;

    QVariant itemData = item->data(static_cast<int>(Qt::UserRole));
    return itemData.value<OBSScene>();
}

QLayout* Utils::GetPreviewLayout() {
    QMainWindow* main = (QMainWindow*)obs_frontend_get_main_window();
    return main->findChild<QLayout*>("previewLayout");
}

void Utils::TransitionToProgram() {
    if (!obs_frontend_preview_program_mode_active())
        return;

    // WARNING : if the layout created in OBS' CreateProgramOptions() changes
    // then this won't work as expected

    QMainWindow* main = (QMainWindow*)obs_frontend_get_main_window();

    // The program options widget is the second item in the left-to-right layout
    QWidget* programOptions = GetPreviewLayout()->itemAt(1)->widget();

    // The "Transition" button lies in the mainButtonLayout
    // which is the first itemin the program options' layout
    QLayout* mainButtonLayout = programOptions->layout()->itemAt(1)->layout();
    QWidget* transitionBtnWidget = mainButtonLayout->itemAt(0)->widget();

    // Try to cast that widget into a button
    QPushButton* transitionBtn = qobject_cast<QPushButton*>(transitionBtnWidget);

    // Perform a click on that button
    transitionBtn->click();
}

const char* Utils::OBSVersionString() {
    uint32_t version = obs_get_version();

    uint8_t major, minor, patch;
    major = (version >> 24) & 0xFF;
    minor = (version >> 16) & 0xFF;
    patch = version & 0xFF;

    char* result = (char*)bmalloc(sizeof(char) * 12);
    sprintf(result, "%d.%d.%d", major, minor, patch);

    return result;
}

QSystemTrayIcon* Utils::GetTrayIcon() {
    QMainWindow* main = (QMainWindow*)obs_frontend_get_main_window();
    return main->findChildren<QSystemTrayIcon*>().first();
}

void Utils::SysTrayNotify(QString &text,
    QSystemTrayIcon::MessageIcon icon, QString title) {
    if (!Config::Current()->AlertsEnabled || !QSystemTrayIcon::supportsMessages())
        return;

    QSystemTrayIcon* trayIcon = GetTrayIcon();
    if (trayIcon)
        trayIcon->showMessage(title, text, icon);
}

QString Utils::FormatIPAddress(QHostAddress &addr) {
    QRegExp v4regex("(::ffff:)(((\\d).){3})", Qt::CaseInsensitive);
    QString addrString = addr.toString();
    if (addrString.contains(v4regex)) {
        addrString = QHostAddress(addr.toIPv4Address()).toString();
    }
    return addrString;
}

const char* Utils::GetRecordingFolder() {
    config_t* profile = obs_frontend_get_profile_config();
    QString outputMode = config_get_string(profile, "Output", "Mode");

    if (outputMode == "Advanced") {
        // Advanced mode
        return config_get_string(profile, "AdvOut", "RecFilePath");
    } else {
        // Simple mode
        return config_get_string(profile, "SimpleOutput", "FilePath");
    }
}

bool Utils::SetRecordingFolder(const char* path) {
    if (!QDir(path).exists())
        return false;

    config_t* profile = obs_frontend_get_profile_config();
    QString outputMode = config_get_string(profile, "Output", "Mode");

    if (outputMode == "Advanced") {
        config_set_string(profile, "AdvOut", "RecFilePath", path);
    } else {
        config_set_string(profile, "SimpleOutput", "FilePath", path);
    }

    config_save(profile);
    return true;
}

QString Utils::ParseDataToQueryString(obs_data_t* data) {
    QString query = QString().null;
    if (data) {
        obs_data_item_t* item = obs_data_first(data);
        if (item) {
            bool isFirst = true;
            do {
                if (!obs_data_item_has_user_value(item))
                    continue;

                if (!isFirst)
                    query.append('&');
                else
                    isFirst = false;

                const char* attrName = obs_data_item_get_name(item);
                query.append(attrName).append("=");

                switch (obs_data_item_gettype(item)) {
                    case OBS_DATA_BOOLEAN:
                        query.append(obs_data_item_get_bool(item)?"true":"false");
                        break;
                    case OBS_DATA_NUMBER:
                        switch (obs_data_item_numtype(item))
                        {
                            case OBS_DATA_NUM_DOUBLE:
                                query.append(
                                    QString::number(obs_data_item_get_double(item)));
                                break;
                            case OBS_DATA_NUM_INT:
                                query.append(
                                    QString::number(obs_data_item_get_int(item)));
                                break;
                            case OBS_DATA_NUM_INVALID:
                                break;
                        }
                        break;
                    case OBS_DATA_STRING:
                        query.append(QUrl::toPercentEncoding(
                            QString(obs_data_item_get_string(item))));
                        break;
                    default:
                        //other types are not supported
                        break;
                }
            } while (obs_data_item_next(&item));
        }
    }
    return query;
}

obs_hotkey_t* Utils::FindHotkeyByName(QString name) {
    struct current_search {
        QString query;
        obs_hotkey_t* result;
    };

    current_search search;
    search.query = name;
    search.result = nullptr;

    obs_enum_hotkeys([](void* data, obs_hotkey_id id, obs_hotkey_t* hotkey) {
        current_search* search = static_cast<current_search*>(data);

        const char* hk_name = obs_hotkey_get_name(hotkey);
        if (hk_name == search->query) {
            search->result = hotkey;
            return false;
        }

        return true;
    }, &search);

    return search.result;
}

bool Utils::ReplayBufferEnabled() {
    config_t* profile = obs_frontend_get_profile_config();
    QString outputMode = config_get_string(profile, "Output", "Mode");

    if (outputMode == "Simple") {
        return config_get_bool(profile, "SimpleOutput", "RecRB");
    }
    else if (outputMode == "Advanced") {
        return config_get_bool(profile, "AdvOut", "RecRB");
    }

    return false;
}

bool Utils::RPHotkeySet() {
    OBSOutputAutoRelease rpOutput = obs_frontend_get_replay_buffer_output();

    OBSDataAutoRelease hotkeys = obs_hotkeys_save_output(rpOutput);
    OBSDataArrayAutoRelease bindings = obs_data_get_array(hotkeys,
        "ReplayBuffer.Save");

    size_t count = obs_data_array_count(bindings);
    return (count > 0);
}
