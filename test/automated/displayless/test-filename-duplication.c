#include <glib.h>

#include <nautilus-filename-utilities.h>


static void
test_file_copy_duplication_name (void)
{
    char *duplicated;

#define ASSERT_DUPLICATION_NAME(ORIGINAL, DUPLICATE) \
        duplicated = nautilus_filename_create_duplicate (ORIGINAL, 1, -1, FALSE); \
        g_assert_cmpstr (duplicated, ==, DUPLICATE); \
        g_free (duplicated);

    /* test the next duplicate name generator */
    ASSERT_DUPLICATION_NAME (" (Copy)", " (Copy 2)");
    ASSERT_DUPLICATION_NAME ("foo", "foo (Copy)");
    ASSERT_DUPLICATION_NAME (".bashrc", ".bashrc (Copy)");
    ASSERT_DUPLICATION_NAME (".foo.txt", ".foo (Copy).txt");
    ASSERT_DUPLICATION_NAME ("foo foo", "foo foo (Copy)");
    ASSERT_DUPLICATION_NAME ("foo.txt", "foo (Copy).txt");
    ASSERT_DUPLICATION_NAME ("foo foo.txt", "foo foo (Copy).txt");
    ASSERT_DUPLICATION_NAME ("foo foo.txt txt", "foo foo.txt txt (Copy)");
    ASSERT_DUPLICATION_NAME ("foo...txt", "foo.. (Copy).txt");
    ASSERT_DUPLICATION_NAME ("foo...", "foo... (Copy)");
    ASSERT_DUPLICATION_NAME ("foo. (Copy)", "foo. (Copy 2)");
    ASSERT_DUPLICATION_NAME ("foo (Copy)", "foo (Copy 2)");
    ASSERT_DUPLICATION_NAME ("foo (Copy).txt", "foo (Copy 2).txt");
    ASSERT_DUPLICATION_NAME ("foo (Copy 2)", "foo (Copy 3)");
    ASSERT_DUPLICATION_NAME ("foo (Copy 2).txt", "foo (Copy 3).txt");
    ASSERT_DUPLICATION_NAME ("foo foo (Copy 2).txt", "foo foo (Copy 3).txt");
    ASSERT_DUPLICATION_NAME ("foo (Copy 13)", "foo (Copy 14)");
    ASSERT_DUPLICATION_NAME ("foo foo (Copy 100000000000000).txt", "foo foo (Copy 100000000000001).txt");

#undef ASSERT_DUPLICATION_NAME

    duplicated = nautilus_filename_create_duplicate ("dir.with.dots", 1, -1, TRUE);
    g_assert_cmpstr (duplicated, ==, "dir.with.dots (Copy)");
    g_free (duplicated);

    duplicated = nautilus_filename_create_duplicate ("dir (copy).dir", 1, -1, TRUE);
    g_assert_cmpstr (duplicated, ==, "dir (Copy).dir (Copy)");
    g_free (duplicated);
}

static char *
format_helper (const char *str)
{
    return g_strdup_printf ("%s-123456789", str);
}

static const char *long_base = "great-text-but-sadly-too-long";
static const char *short_base = "great-text";

static void
test_file_name_shortening_with_base (void)
{
    g_autofree char *filename = format_helper (long_base);
    g_autofree char *desired = format_helper (short_base);
    size_t max_length = 20;
    gboolean shortened;

    g_assert_cmpuint (strlen (filename), >, max_length);

    shortened = nautilus_filename_shorten_base (&filename, long_base, max_length);

    g_assert_true (shortened);

    g_assert_cmpuint (strlen (filename), <=, max_length);

    g_assert_cmpstr (filename, ==, desired);
}

static void
test_file_name_shortening_with_base_not_needed (void)
{
    g_autofree char *filename = format_helper (short_base);
    g_autofree char *desired = format_helper (short_base);
    size_t max_length = 20;
    gboolean shortened;

    g_assert_cmpuint (strlen (filename), <=, max_length);

    shortened = nautilus_filename_shorten_base (&filename, short_base, max_length);

    g_assert_false (shortened);

    g_assert_cmpuint (strlen (filename), <=, max_length);

    g_assert_cmpstr (filename, ==, desired);
}

int
main (int   argc,
      char *argv[])
{
    g_test_init (&argc, &argv, NULL);
    g_test_set_nonfatal_assertions ();

    g_test_add_func ("/file-copy-duplictation-name/1.0",
                     test_file_copy_duplication_name);
    g_test_add_func ("/file-name-shortening-with-base/needed",
                     test_file_name_shortening_with_base);
    g_test_add_func ("/file-name-shortening-with-base/not-needed",
                     test_file_name_shortening_with_base_not_needed);

    return g_test_run ();
}
