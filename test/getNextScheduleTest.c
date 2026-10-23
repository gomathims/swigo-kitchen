

// Include the header file for the function TimeSchedule getNextSchedule(DateTime currentDateTime);
#include "../timesettings.h"

TEST(GetNextSchedule, NoSchedules) {
  // Test when there are no schedules
  DateTime testDateTime(2022, 10, 20, 12, 0, 0);
  TimeSchedule result = getNextSchedule(testDateTime);

  // Assert that the returned schedule is empty
  ASSERT_EQ(result.startHour, 0);
  ASSERT_EQ(result.startMinute, 0);
  ASSERT_EQ(result.endHour, 0);
  ASSERT_EQ(result.endMinute, 0);
}

TEST(GetNextSchedule, WithinScheduleRange) {
  // Test when the current time is within a schedule's range
  DateTime testDateTime(2022, 10, 20, 8, 30, 0);
  TimeSchedule result = getNextSchedule(testDateTime);

  // Assert that the correct schedule is returned
  ASSERT_EQ(result.startHour, 8);
  ASSERT_EQ(result.startMinute, 0);
  ASSERT_EQ(result.endHour, 9);
  ASSERT_EQ(result.endMinute, 0);
}

TEST(GetNextSchedule, NextScheduleCloser) {
  // Test when the next schedule is closer to the current time
  DateTime testDateTime(2022, 10, 20, 15, 0, 0);
  TimeSchedule result = getNextSchedule(testDateTime);

  // Assert that the correct next schedule is returned
  ASSERT_EQ(result.startHour, 16);
  ASSERT_EQ(result.startMinute, 0);
  ASSERT_EQ(result.endHour, 17);
  ASSERT_EQ(result.endMinute, 0);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}