/*
 * Copyright (C) 2010, Nokia <ivan.frade@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <glib-object.h>

/* NOTE: We're not including tracker-miner.h here because this is private. */
#include <libtracker-miner/tracker-priority-queue.h>

static void
test_priority_queue_ref_unref (void)
{
        TrackerPriorityQueue *one, *two;
        
        one = tracker_priority_queue_new ();
        two = tracker_priority_queue_ref (one);

        tracker_priority_queue_unref (two);
        tracker_priority_queue_unref (one);
}

static void
test_priority_queue_emptiness (void)
{
        TrackerPriorityQueue *one;
        
        one = tracker_priority_queue_new ();

        g_assert (tracker_priority_queue_is_empty (one));
        g_assert_cmpint (tracker_priority_queue_get_length (one), ==, 0);

        tracker_priority_queue_unref (one);
}

static void
test_priority_queue_insertion_pop (void)
{
        TrackerPriorityQueue *queue;
        int                   i, priority;
        gchar                *text, *expected;

        queue = tracker_priority_queue_new ();

        /* Insert in to loops to "mix" priorities in the insertion */
        for (i = 1; i <= 10; i+=2) {
                tracker_priority_queue_add (queue, g_strdup_printf ("test content %i", i), i);
        }

        for (i = 2; i <= 10; i+=2) {
                tracker_priority_queue_add (queue, g_strdup_printf ("test content %i", i), i);
        }

        for (i = 1; i <= 10; i++) {
                expected = g_strdup_printf ("test content %i", i);

                text = (gchar *)tracker_priority_queue_pop (queue, &priority);

                g_assert_cmpint (priority, ==, i);
                g_assert_cmpstr (text, ==, expected);

                g_free (expected);
                g_free (text);
        }

        g_assert (tracker_priority_queue_is_empty (queue));        
        tracker_priority_queue_unref (queue);
}

static void
test_priority_queue_peek (void)
{
        TrackerPriorityQueue *queue;
        gchar                *result;
        gint                  priority;

        queue = tracker_priority_queue_new ();

        result = tracker_priority_queue_peek (queue, &priority);
        g_assert (result == NULL);

        tracker_priority_queue_add (queue, g_strdup ("Low prio"), 10);
        tracker_priority_queue_add (queue, g_strdup ("High prio"), 1);

        result = tracker_priority_queue_peek (queue, &priority);
        g_assert_cmpint (priority, ==, 1);
        g_assert_cmpstr (result, ==, "High prio");

        result = tracker_priority_queue_pop (queue, &priority);
        g_free (result);

        result = tracker_priority_queue_peek (queue, &priority);
        g_assert_cmpint (priority, ==, 10);
        g_assert_cmpstr (result, ==, "Low prio");

        result = tracker_priority_queue_pop (queue, &priority);
        g_free (result);

        tracker_priority_queue_unref (queue);
}

static void
test_priority_queue_find (void)
{
        TrackerPriorityQueue *queue;
        gchar *result;
        int priority;

        queue = tracker_priority_queue_new ();
        
        tracker_priority_queue_add (queue, g_strdup ("search me"), 10);
        tracker_priority_queue_add (queue, g_strdup ("Not me"), 1);
        tracker_priority_queue_add (queue, g_strdup ("Not me either"), 20);

        result = (gchar *) tracker_priority_queue_find (queue, &priority, g_str_equal, "search me");
        g_assert_cmpstr (result, !=, NULL);
        g_assert_cmpint (priority, ==, 10);

        while (!tracker_priority_queue_is_empty (queue)) {
            gchar *text = (gchar *) tracker_priority_queue_pop (queue, NULL);
            g_free (text);
        }

        tracker_priority_queue_unref (queue);
}

static void
foreach_testing_cb (G_GNUC_UNUSED gpointer data,
                                  gpointer user_data)
{
        gint *counter = (gint *)user_data;
        (*counter) += 1;
}

static void
test_priority_queue_foreach (void)
{
        TrackerPriorityQueue *queue;
        gint                  counter = 0;

        queue = tracker_priority_queue_new ();
        
        tracker_priority_queue_add (queue, g_strdup ("x"), 10);
        tracker_priority_queue_add (queue, g_strdup ("x"), 20);        
        tracker_priority_queue_add (queue, g_strdup ("x"), 30);

        tracker_priority_queue_foreach (queue, foreach_testing_cb, &counter);

        g_assert_cmpint (counter, ==, 3);

        while (!tracker_priority_queue_is_empty (queue)) {
            gchar *text = (gchar *) tracker_priority_queue_pop (queue, NULL);
            g_free (text);
        }

        tracker_priority_queue_unref (queue);
}

static void
test_priority_queue_foreach_remove (void)
{
        TrackerPriorityQueue *queue;
        
        queue = tracker_priority_queue_new ();

        tracker_priority_queue_add (queue, g_strdup ("y"), 1);
        tracker_priority_queue_add (queue, g_strdup ("x"), 2);
        tracker_priority_queue_add (queue, g_strdup ("y"), 3);
        tracker_priority_queue_add (queue, g_strdup ("x"), 4);
        tracker_priority_queue_add (queue, g_strdup ("y"), 5);
        g_assert_cmpint (tracker_priority_queue_get_length (queue), ==, 5);
        
        tracker_priority_queue_foreach_remove (queue, g_str_equal, "y", g_free);
        g_assert_cmpint (tracker_priority_queue_get_length (queue), ==, 2);

        tracker_priority_queue_foreach_remove (queue, g_str_equal, "x", g_free);
        g_assert_cmpint (tracker_priority_queue_get_length (queue), ==, 0);

        tracker_priority_queue_unref (queue);
}

static void
test_priority_queue_branches (void)
{

        /* Few specific testing to improve the branch coverage */

        TrackerPriorityQueue *queue;
        gchar                *result;
        gint                  priority;

        queue = tracker_priority_queue_new ();

        /* Removal on empty list */
        tracker_priority_queue_foreach_remove (queue, g_str_equal, "y", g_free);


        /* Insert multiple elements in the same priority */
        tracker_priority_queue_add (queue, g_strdup ("x"), 5);
        tracker_priority_queue_add (queue, g_strdup ("y"), 5);
        tracker_priority_queue_add (queue, g_strdup ("z"), 5);

        g_assert_cmpint (tracker_priority_queue_get_length (queue), ==, 3);

        /* Removal with multiple elements in same priority*/
        g_assert (tracker_priority_queue_foreach_remove (queue, g_str_equal, "z", g_free));
        g_assert (tracker_priority_queue_foreach_remove (queue, g_str_equal, "x", g_free));
        

        /* Pop those elements */
        result = tracker_priority_queue_pop (queue, &priority);
        g_assert_cmpint (priority, ==, 5);
        g_free (result);

        g_assert_cmpint (tracker_priority_queue_get_length (queue), ==, 0);
        /* Pop on empty queue */
        result = tracker_priority_queue_pop (queue, &priority);
        g_assert (result == NULL);

        tracker_priority_queue_unref (queue);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/libtracker-miner/tracker-priority-queue/emptiness",
	                 test_priority_queue_emptiness);
	g_test_add_func ("/libtracker-miner/tracker-priority-queue/ref_unref",
	                 test_priority_queue_ref_unref);
	g_test_add_func ("/libtracker-miner/tracker-priority-queue/insertion",
	                 test_priority_queue_insertion_pop);
	g_test_add_func ("/libtracker-miner/tracker-priority-queue/peek",
	                 test_priority_queue_peek);
	g_test_add_func ("/libtracker-miner/tracker-priority-queue/find",
	                 test_priority_queue_find);
	g_test_add_func ("/libtracker-miner/tracker-priority-queue/foreach",
	                 test_priority_queue_foreach);
	g_test_add_func ("/libtracker-miner/tracker-priority-queue/foreach_remove",
	                 test_priority_queue_foreach_remove);

        g_test_add_func ("/libtracker-miner/tracker-priority-queue/branches",
                         test_priority_queue_branches);

	return g_test_run ();
}
