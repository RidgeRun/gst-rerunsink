#include <gst/check/gstcheck.h>
#include "gstrerunsink.h"

GST_START_TEST(test_element_exists)
{
  GstElement *rerunsink = gst_element_factory_make("rerunsink", NULL);
  fail_unless(rerunsink != NULL, "Failed to create rerunsink element");
  gst_object_unref(rerunsink);
}
GST_END_TEST

GST_START_TEST(test_is_videosink)
{
  GstElement *rerunsink = gst_element_factory_make("rerunsink", NULL);
  fail_unless(GST_IS_VIDEO_SINK(rerunsink), "rerunsink is not a GstVideoSink");
  gst_object_unref(rerunsink);
}
GST_END_TEST

GST_START_TEST(test_simple_pipeline)
{
  GstElement *pipeline = gst_parse_launch(
      "videotestsrc num-buffers=1 ! video/x-raw,format=RGB ! rerunsink", NULL);
  fail_unless(pipeline != NULL, "Pipeline creation failed");

  GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
  fail_unless(ret != GST_STATE_CHANGE_FAILURE, "Pipeline failed to start");

  gst_element_get_state(pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);
}
GST_END_TEST

static Suite* rerunsink_suite(void)
{
  Suite *s = suite_create("rerunsink");
  TCase *tc = tcase_create("core");

  tcase_add_test(tc, test_element_exists);
  tcase_add_test(tc, test_is_videosink);
  tcase_add_test(tc, test_simple_pipeline);

  suite_add_tcase(s, tc);
  return s;
}

int main(int argc, char **argv)
{
  gst_init(&argc, &argv);
  g_test_init(&argc, &argv, NULL);
  return g_test_run_suite(rerunsink_suite());
}

