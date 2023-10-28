#include <glib.h>

#include <nautilus-directory-private.h>
#include <nautilus-file.h>
#include <nautilus-file-private.h>
#include <nautilus-file-utilities.h>


static void
test_file_refcount_single_file (void)
{
    NautilusFile *file;

    g_assert_cmpint (nautilus_directory_number_outstanding (), ==, 0);

    file = nautilus_file_get_by_uri ("file:///home/");

    g_assert_cmpint (G_OBJECT (file)->ref_count, ==, 1);
    g_assert_cmpint (G_OBJECT (file->details->directory)->ref_count, ==, 1);
    g_assert_cmpint (nautilus_directory_number_outstanding (), ==, 1);

    nautilus_file_unref (file);

    g_assert_cmpint (nautilus_directory_number_outstanding (), ==, 0);
}

static void
test_file_refcount_file_list (void)
{
    NautilusFile *file_1 = nautilus_file_get_by_uri ("file:///etc");
    NautilusFile *file_2 = nautilus_file_get_by_uri ("file:///usr");

    GList *list = NULL;
    list = g_list_prepend (list, file_1);
    list = g_list_prepend (list, file_2);
    nautilus_file_list_ref (list);

    g_assert_cmpint (G_OBJECT (file_1)->ref_count, ==, 2);
    g_assert_cmpint (G_OBJECT (file_2)->ref_count, ==, 2);

    nautilus_file_list_unref (list);

    g_assert_cmpint (G_OBJECT (file_1)->ref_count, ==, 1);
    g_assert_cmpint (G_OBJECT (file_2)->ref_count, ==, 1);

    nautilus_file_list_free (list);

    g_assert_cmpint (nautilus_directory_number_outstanding (), ==, 0);
}

static void
test_file_check_name (void)
{
    g_autoptr (NautilusFile) file = nautilus_file_get_by_uri ("file:///home/");
    g_autofree char *name = nautilus_file_get_name (file);

    g_assert_cmpstr (name, ==, "home");
}

static void
test_file_duplicate_pointers (void)
{
    g_autoptr (NautilusFile) file = nautilus_file_get_by_uri ("file:///home/");
    g_autofree char *name = NULL;

    g_assert_true (nautilus_file_get_by_uri ("file:///home/") == file);
    nautilus_file_unref (file);

    g_assert_true (nautilus_file_get_by_uri ("file:///home") == file);
    nautilus_file_unref (file);

    name = nautilus_file_get_name (file);
    g_assert_cmpstr (name, ==, "home");
}

static void
test_file_sort_order (void)
{
    g_autoptr (NautilusFile) file_1 = nautilus_file_get_by_uri ("file:///etc");
    g_autoptr (NautilusFile) file_2 = nautilus_file_get_by_uri ("file:///usr");
    NautilusFileSortType sort_type = NAUTILUS_FILE_SORT_BY_DISPLAY_NAME;
    int order;

    g_assert_cmpint (G_OBJECT (file_1)->ref_count, ==, 1);
    g_assert_cmpint (G_OBJECT (file_2)->ref_count, ==, 1);

    order = nautilus_file_compare_for_sort (file_1, file_2, sort_type, FALSE, FALSE);
    g_assert_cmpint (order, <, 0);

    order = nautilus_file_compare_for_sort (file_1, file_2, sort_type, FALSE, TRUE);
    g_assert_cmpint (order, >, 0);
}

static void
test_file_sort_with_self (void)
{
    g_autoptr (NautilusFile) file_1 = nautilus_file_get_by_uri ("file:///etc");
    NautilusFileSortType sort_type = NAUTILUS_FILE_SORT_BY_DISPLAY_NAME;
    int order;

    order = nautilus_file_compare_for_sort (file_1, file_1, sort_type, TRUE, TRUE);
    g_assert_cmpint (order, ==, 0);

    order = nautilus_file_compare_for_sort (file_1, file_1, sort_type, TRUE, FALSE);
    g_assert_cmpint (order, ==, 0);

    order = nautilus_file_compare_for_sort (file_1, file_1, sort_type, FALSE, TRUE);
    g_assert_cmpint (order, ==, 0);

    order = nautilus_file_compare_for_sort (file_1, file_1, sort_type, FALSE, FALSE);
    g_assert_cmpint (order, ==, 0);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_set_nonfatal_assertions ();
    nautilus_ensure_extension_points ();

    g_test_add_func ("/file-refcount/single-file",
                     test_file_refcount_single_file);
    g_test_add_func ("/file-refcount/file-list",
                     test_file_refcount_file_list);
    g_test_add_func ("/file-check-name/1.0",
                     test_file_check_name);
    g_test_add_func ("/file-duplicate-pointers/1.0",
                     test_file_duplicate_pointers);
    g_test_add_func ("/file-sort/order",
                     test_file_sort_order);
    g_test_add_func ("/file-sort/with-self",
                     test_file_sort_with_self);

    return g_test_run ();
}
