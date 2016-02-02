/*
 * Copyright 2014 Canonical Ltd.
 *
 * Authors:
 *   Charles Kerr <charles.kerr@canonical.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3, as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranties of
 * MERCHANTABILITY, SATISFACTORY QUALITY, or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <datetime/appointment.h>
#include <datetime/dbus-shared.h>
#include <datetime/settings.h>
#include <datetime/snap.h>

#include <notifications/dbus-shared.h>
#include <notifications/notifications.h>

#include "notification-fixture.h"

using namespace ayatana::indicator::datetime;

namespace uin = ayatana::indicator::notifications;

/***
****
***/

namespace
{
  static constexpr char const * APP_NAME {"ayatana-indicator-datetime-service"};
}

using namespace ayatana::indicator::datetime;

/***
****
***/

namespace
{
  gboolean quit_idle (gpointer gloop)
  {
    g_main_loop_quit(static_cast<GMainLoop*>(gloop));
    return G_SOURCE_REMOVE;
  };
}

TEST_F(NotificationFixture, InteractiveDuration)
{
  static constexpr int duration_minutes = 120;
  auto settings = std::make_shared<Settings>();
  settings->alarm_duration.set(duration_minutes);
  auto ne = std::make_shared<ayatana::indicator::notifications::Engine>(APP_NAME);
  auto sb = std::make_shared<ayatana::indicator::notifications::DefaultSoundBuilder>();
  auto snap = create_snap(ne, sb, settings);

  make_interactive();

  // call the Snap Decision
  auto func = [this](const Appointment&, const Alarm&){g_idle_add(quit_idle, loop);};
  (*snap)(appt, appt.alarms.front(), func, func);

  // confirm that Notify got called once
  guint len = 0;
  GError * error = nullptr;
  const auto calls = dbus_test_dbus_mock_object_get_method_calls (notify_mock,
                                                                  notify_obj,
                                                                  METHOD_NOTIFY,
                                                                  &len,
                                                                  &error);
  g_assert_no_error(error);
  ASSERT_EQ(1, len);

  // confirm that the app_name passed to Notify was APP_NAME
  const auto& params = calls[0].params;
  ASSERT_EQ(8, g_variant_n_children(params));
  const char * str = nullptr;
  g_variant_get_child (params, 0, "&s", &str);
  ASSERT_STREQ(APP_NAME, str);

  // confirm that the icon passed to Notify was "alarm-clock"
  g_variant_get_child (params, 2, "&s", &str);
  ASSERT_STREQ("alarm-clock", str);

  // confirm that the hints passed to Notify included a timeout matching duration_minutes
  int32_t i32;
  bool b;
  auto hints = g_variant_get_child_value (params, 6);
  b = g_variant_lookup (hints, HINT_TIMEOUT, "i", &i32);
  EXPECT_TRUE(b);
  const auto duration = std::chrono::minutes(duration_minutes);
  EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count(), i32);
  g_variant_unref(hints);
  ne.reset();
}

/***
****
***/

TEST_F(NotificationFixture, InhibitSleep)
{
  auto settings = std::make_shared<Settings>();
  auto ne = std::make_shared<ayatana::indicator::notifications::Engine>(APP_NAME);
  auto sb = std::make_shared<ayatana::indicator::notifications::DefaultSoundBuilder>();
  auto snap = create_snap(ne, sb, settings);

  make_interactive();

  // invoke the notification
  auto func = [this](const Appointment&, const Alarm&){g_idle_add(quit_idle, loop);};
  (*snap)(appt, appt.alarms.front(), func, func);

  wait_msec(1000);

  // confirm that sleep got inhibited
  GError * error = nullptr;
  EXPECT_TRUE (dbus_test_dbus_mock_object_check_method_call (powerd_mock,
                                                             powerd_obj,
                                                             POWERD_METHOD_REQUEST_SYS_STATE,
                                                             g_variant_new("(si)", APP_NAME, POWERD_SYS_STATE_ACTIVE),
                                                             &error));

  // confirm that the screen got forced on
  EXPECT_TRUE (dbus_test_dbus_mock_object_check_method_call (screen_mock,
                                                             screen_obj,
                                                             SCREEN_METHOD_KEEP_DISPLAY_ON,
                                                             nullptr,
                                                             &error));

  // force-close the snap
  wait_msec(100);
  snap.reset();
  wait_msec(100);

  // confirm that sleep got uninhibted
  EXPECT_TRUE (dbus_test_dbus_mock_object_check_method_call (powerd_mock,
                                                             powerd_obj,
                                                             POWERD_METHOD_CLEAR_SYS_STATE,
                                                             g_variant_new("(s)", POWERD_COOKIE),
                                                             &error));

  // confirm that the screen's no longer forced on
  EXPECT_TRUE (dbus_test_dbus_mock_object_check_method_call (screen_mock,
                                                             screen_obj,
                                                             SCREEN_METHOD_REMOVE_DISPLAY_ON_REQUEST,
                                                             g_variant_new("(i)", SCREEN_COOKIE),
                                                             &error));

  g_assert_no_error (error);
}

/***
****
***/

TEST_F(NotificationFixture, ForceScreen)
{
  auto settings = std::make_shared<Settings>();
  auto ne = std::make_shared<ayatana::indicator::notifications::Engine>(APP_NAME);
  auto sb = std::make_shared<ayatana::indicator::notifications::DefaultSoundBuilder>();
  auto snap = create_snap(ne, sb, settings);

  make_interactive();

  // invoke the notification
  auto func = [this](const Appointment&, const Alarm&){g_idle_add(quit_idle, loop);};
  (*snap)(appt, appt.alarms.front(), func, func);

  wait_msec(1000);

  // confirm that sleep got inhibited
  GError * error = nullptr;
  EXPECT_TRUE (dbus_test_dbus_mock_object_check_method_call (powerd_mock,
                                                             powerd_obj,
                                                             POWERD_METHOD_REQUEST_SYS_STATE,
                                                             g_variant_new("(si)", APP_NAME, POWERD_SYS_STATE_ACTIVE),
                                                             &error));
  g_assert_no_error(error);

  // force-close the snap
  wait_msec(100);
  snap.reset();
  wait_msec(100);

  // confirm that sleep got uninhibted
  EXPECT_TRUE (dbus_test_dbus_mock_object_check_method_call (powerd_mock,
                                                             powerd_obj,
                                                             POWERD_METHOD_CLEAR_SYS_STATE,
                                                             g_variant_new("(s)", POWERD_COOKIE),
                                                             &error));
  g_assert_no_error(error);
}

/***
****
***/

/**
 * A DefaultSoundBuilder wrapper which remembers the parameters of the last sound created.
 */
class TestSoundBuilder: public uin::SoundBuilder
{
public:
    TestSoundBuilder() =default;
    ~TestSoundBuilder() =default;

    virtual std::shared_ptr<uin::Sound> create(const std::string& role, const std::string& uri, unsigned int volume, bool loop) override {
        m_role = role;
        m_uri = uri;
        m_volume = volume;
        m_loop = loop;
        return m_impl.create(role, uri, volume, loop);
    }

    const std::string& role() { return m_role; }
    const std::string& uri() { return m_uri; }
    unsigned int volume() { return m_volume; }
    bool loop() { return m_loop; }

private:
    std::string m_role;
    std::string m_uri;
    unsigned int m_volume;
    bool m_loop;
    uin::DefaultSoundBuilder m_impl;
};

std::string path_to_uri(const std::string& path)
{
  auto file = g_file_new_for_path(path.c_str());
  auto uri_cstr = g_file_get_uri(file);
  std::string uri = uri_cstr;
  g_free(uri_cstr);
  g_clear_pointer(&file, g_object_unref);
  return uri;
}

TEST_F(NotificationFixture,DefaultSounds)
{
  auto settings = std::make_shared<Settings>();
  auto ne = std::make_shared<ayatana::indicator::notifications::Engine>(APP_NAME);
  auto sb = std::make_shared<TestSoundBuilder>();
  auto func = [this](const Appointment&, const Alarm&){g_idle_add(quit_idle, loop);};

  const struct {
      Appointment appointment;
      std::string expected_role;
      std::string expected_uri;
  } test_cases[] = {
      { ualarm, "alarm", path_to_uri(ALARM_DEFAULT_SOUND) },
      { appt,   "alert", path_to_uri(CALENDAR_DEFAULT_SOUND) }
  };

  auto snap = create_snap(ne, sb, settings);

  for(const auto& test_case : test_cases)
  {
    (*snap)(test_case.appointment, test_case.appointment.alarms.front(), func, func);
    wait_msec(100);
    EXPECT_EQ(test_case.expected_uri, sb->uri());
    EXPECT_EQ(test_case.expected_role, sb->role());
  }
}

/***
****
***/

TEST_F(NotificationFixture,Notification)
{
  // Feed different combinations of system settings,
  // indicator-datetime settings, and event types,
  // then see if notifications and haptic feedback behave as expected.

  auto settings = std::make_shared<Settings>();
  auto ne = std::make_shared<ayatana::indicator::notifications::Engine>(APP_NAME);
  auto sb = std::make_shared<ayatana::indicator::notifications::DefaultSoundBuilder>();
  auto func = [this](const Appointment&, const Alarm&){g_idle_add(quit_idle, loop);};

  // combinatorial factor #1: event type
  struct {
    Appointment appt;
    bool expected_notify_called;
    bool expected_vibrate_called;
  } test_appts[] = {
    { appt, true, true },
    { ualarm, true, true }
  };

  // combinatorial factor #2: indicator-datetime's haptic mode
  struct {
    const char* haptic_mode;
    bool expected_notify_called;
    bool expected_vibrate_called;
  } test_haptics[] = {
    { "none", true, false },
    { "pulse", true, true }
  };

  // combinatorial factor #3: system settings' "other vibrations" enabled
  struct {
    bool other_vibrations;
    bool expected_notify_called;
    bool expected_vibrate_called;
  } test_other_vibrations[] = {
    { true, true, true },
    { false, true, false }
  };

  // combinatorial factor #4: system settings' notifications app blacklist
  const std::set<std::pair<std::string,std::string>> blacklist_calendar { std::make_pair(std::string{"com.lomiri.calendar"}, std::string{"calendar-app"}) };
  const std::set<std::pair<std::string,std::string>> blacklist_empty;
  struct {
    std::set<std::pair<std::string,std::string>> muted_apps; // apps that should not trigger notifications
    bool expected_notify_called; // do we expect the notification tho show?
    bool expected_vibrate_called; // do we expect the phone to vibrate?
  } test_muted_apps[] = {
    { blacklist_calendar, false, false },
    { blacklist_empty, true, true }
  };

  for (const auto& test_appt : test_appts)
  {
  for (const auto& test_haptic : test_haptics)
  {
  for (const auto& test_vibes : test_other_vibrations)
  {
  for (const auto& test_muted : test_muted_apps)
  {
  auto snap = create_snap(ne, sb, settings);

    const bool expected_notify_called = test_appt.expected_notify_called
                                     && test_vibes.expected_notify_called
                                     && test_muted.expected_notify_called
                                     && test_haptic.expected_notify_called;

    const bool expected_vibrate_called = test_appt.expected_vibrate_called
                                      && test_vibes.expected_vibrate_called
                                      && test_muted.expected_vibrate_called
                                      && test_haptic.expected_vibrate_called;

    // clear out any previous iterations' noise
    GError * error = nullptr;
    dbus_test_dbus_mock_object_clear_method_calls(haptic_mock, haptic_obj, &error);
    g_assert_no_error(error);
    dbus_test_dbus_mock_object_clear_method_calls(notify_mock, notify_obj, &error);
    g_assert_no_error(error);

    // set the properties to match the test case
    settings->muted_apps.set(test_muted.muted_apps);
    settings->alarm_haptic.set(test_haptic.haptic_mode);
    dbus_test_dbus_mock_object_update_property(as_mock,
                                               as_obj,
                                               PROP_OTHER_VIBRATIONS,
                                               g_variant_new_boolean(test_vibes.other_vibrations),
                                               &error);
    g_assert_no_error(error);
    wait_msec(100);

    // run the test
    (*snap)(appt, appt.alarms.front(), func, func);
    wait_msec(100);

    // test that the notification was as expected
    const bool notify_called = dbus_test_dbus_mock_object_check_method_call(notify_mock,
                                                                            notify_obj,
                                                                            METHOD_NOTIFY,
                                                                            nullptr,
                                                                            &error);
    g_assert_no_error(error);
    EXPECT_EQ(expected_notify_called, notify_called);

    // test that the vibration was as expected
    const bool vibrate_called = dbus_test_dbus_mock_object_check_method_call(haptic_mock,
                                                                             haptic_obj,
                                                                             HAPTIC_METHOD_VIBRATE_PATTERN,
                                                                             nullptr,
                                                                             &error);
    g_assert_no_error(error);
    EXPECT_EQ(expected_vibrate_called, vibrate_called);
  }
  }
  }
  }
}
