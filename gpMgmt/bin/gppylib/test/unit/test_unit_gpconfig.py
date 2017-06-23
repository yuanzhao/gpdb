import base64
import imp
import os
import pickle
import sys
import tempfile

from StringIO import StringIO
from pygresql.pg import DatabaseError

from gparray import GpDB, GpArray, Segment
import shutil
from mock import *
from gp_unittest import *
from gphostcache import GpHost

db_singleton_side_effect_list = []


def singleton_side_effect(unused1, unused2):
    # this function replaces dbconn.execSQLForSingleton(conn, sql), conditionally raising exception
    if db_singleton_side_effect_list[0] == "DatabaseError":
        raise DatabaseError("mock exception")
    return db_singleton_side_effect_list[0]


class GpConfig(GpTestCase):
    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.postgressql_conf = self.temp_dir + "/postgresql.conf"
        with open(self.postgressql_conf, "w") as postgresql:
            postgresql.close()

        # because gpconfig does not have a .py extension,
        # we have to use imp to import it
        # if we had a gpconfig.py, this is equivalent to:
        #   import gpconfig
        #   self.subject = gpconfig
        gpconfig_file = os.path.abspath(os.path.dirname(__file__) + "/../../../gpconfig")
        self.subject = imp.load_source('gpconfig', gpconfig_file)
        self.subject.LOGGER = Mock(spec=['log', 'warn', 'info', 'debug', 'error', 'warning', 'fatal'])

        self.conn = Mock()
        self.cursor = FakeCursor()

        self.os_env = dict(USER="my_user")
        self.os_env["MASTER_DATA_DIRECTORY"] = self.temp_dir
        self.gparray = self._create_gparray_with_2_primary_2_mirrors()
        self.host_cache = Mock()

        self.host = GpHost('localhost')
        seg = Segment()
        db = self.gparray.master
        seg.addPrimary(db)
        seg.datadir = self.gparray.master.datadir
        seg.hostname = 'localhost'
        self.host.addDB(seg)

        self.host_cache.get_hosts.return_value = [self.host]
        self.host_cache.ping_hosts.return_value = []

        self.master_read_config = Mock()
        self.master_read_config.get_guc_value.return_value = "foo"
        self.master_read_config.get_seg_content_id.return_value = -1
        self.segment_read_config = Mock()
        self.segment_read_config.get_guc_value.return_value = "foo"
        self.segment_read_config.get_seg_content_id.return_value = 0

        self.pool = Mock()
        self.pool.getCompletedItems.return_value = [self.master_read_config, self.segment_read_config]

        self.apply_patches([
            patch('os.environ', new=self.os_env),
            patch('gpconfig.dbconn.connect', return_value=self.conn),
            patch('gpconfig.dbconn.execSQL', return_value=self.cursor),
            patch('gpconfig.dbconn.execSQLForSingleton', side_effect=singleton_side_effect),
            patch('gpconfig.GpHostCache', return_value=self.host_cache),
            patch('gpconfig.GpArray.initFromCatalog', return_value=self.gparray),
            patch('gpconfig.GpReadConfig', return_value=self.master_read_config),
            patch('gpconfig.WorkerPool', return_value=self.pool)
        ])
        sys.argv = ["gpconfig"]  # reset to relatively empty args list

    def tearDown(self):
        shutil.rmtree(self.temp_dir)
        super(GpConfig, self).tearDown()
        del db_singleton_side_effect_list[:]

    def test_when_no_options_prints_and_raises(self):
        with self.assertRaisesRegexp(Exception, "No action specified.  See the --help info."):
            self.subject.do_main()
        self.subject.LOGGER.error.assert_called_once_with("No action specified.  See the --help info.")

    def test_option_list_parses(self):
        sys.argv = ["gpconfig", "--list"]
        options = self.subject.parseargs()

        self.assertEquals(options.list, True)

    def test_option_value_must_accompany_option_change_raise(self):
        sys.argv = ["gpconfig", "--change", "statement_mem"]
        with self.assertRaisesRegexp(Exception, "change requested but value not specified"):
            self.subject.parseargs()
        self.subject.LOGGER.error.assert_called_once_with("change requested but value not specified")

    def test_option_show_without_master_data_dir_will_succeed(self):
        sys.argv = ["gpconfig", "--show", "statement_mem"]
        del self.os_env["MASTER_DATA_DIRECTORY"]
        self.subject.parseargs()

    @patch('sys.stdout', new_callable=StringIO)
    def test_option_show_with_port_will_succeed(self, mock_stdout):
        sys.argv = ["gpconfig", "--show", "port"]
        # select * from gp_toolkit.gp_param_setting('port');                                                                                                                     ;
        # paramsegment | paramname | paramvalue
        # --------------+-----------+------------
        self.cursor.set_result_for_testing([['-1', 'port', '1234'], ['0', 'port', '3456']])

        self.subject.do_main()

        self.assertIn("GUC                 : port\nContext:    -1 Value: 1234\nContext:     0 Value: 3456\n",
                      mock_stdout.getvalue())

    def test_option_f_parses(self):
        sys.argv = ["gpconfig", "--file", "--show", "statement_mem"]
        options = self.subject.parseargs()

        self.assertEquals(options.show, "statement_mem")
        self.assertEquals(options.file, True)

    def test_option_file_with_option_change_will_raise(self):
        sys.argv = ["gpconfig", "--file", "--change", "statement_mem"]
        with self.assertRaisesRegexp(Exception, "'--file' option must accompany '--show' option"):
            self.subject.parseargs()
        self.subject.LOGGER.error.assert_called_once_with("'--file' option must accompany '--show' option")

    def test_option_file_compare_with_file_will_raise(self):
        sys.argv = ["gpconfig", "--file", "--show", "statement_mem", "--file-compare", ]
        with self.assertRaisesRegexp(Exception, "'--file' option and '--file-compare' option cannot be used together"):
            self.subject.parseargs()
        self.subject.LOGGER.error.assert_called_once_with("'--file' option and '--file-compare' option cannot be used together")

    def test_option_file_with_option_list_will_raise(self):
        sys.argv = ["gpconfig", "--file", "--list", "statement_mem"]
        with self.assertRaisesRegexp(Exception, "'--file' option must accompany '--show' option"):
            self.subject.parseargs()
        self.subject.LOGGER.error.assert_called_once_with("'--file' option must accompany '--show' option")

    def test_option_file_without_master_data_dir_will_raise(self):
        sys.argv = ["gpconfig", "--file", "--show", "statement_mem"]
        del self.os_env["MASTER_DATA_DIRECTORY"]
        with self.assertRaisesRegexp(Exception, "--file option requires that MASTER_DATA_DIRECTORY be set"):
            self.subject.parseargs()
        self.subject.LOGGER.error.assert_called_once_with("--file option requires that MASTER_DATA_DIRECTORY be set")

    @patch('sys.stdout', new_callable=StringIO)
    def test_option_f_will_report_presence_of_setting(self, mock_stdout):
        sys.argv = ["gpconfig", "--show", "my_property_name", "--file"]

        self.subject.do_main()

        self.pool.addCommand.assert_called_once_with(self.master_read_config)
        self.pool.join.assert_called_once_with()
        self.pool.check_results.assert_called_once_with()
        self.pool.haltWork.assert_called_once_with()
        self.pool.joinWorkers.assert_called_once_with()
        self.assertEqual(self.subject.LOGGER.error.call_count, 0)
        self.assertIn("Master  value: foo\nSegment value: foo", mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=StringIO)
    def test_option_f_will_report_absence_of_setting(self, mock_stdout):
        sys.argv = ["gpconfig", "--show", "my_property_name", "--file"]
        self.master_read_config.get_guc_value.return_value = "-"
        self.segment_read_config.get_guc_value.return_value = "seg_value"

        self.subject.do_main()

        self.assertEqual(self.subject.LOGGER.error.call_count, 0)
        self.assertIn("Master  value: -\nSegment value: seg_value", mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=StringIO)
    def test_option_f_will_report_difference_segments_out_of_sync(self, mock_stdout):
        sys.argv = ["gpconfig", "--show", "my_property_name", "--file"]
        self.master_read_config.get_guc_value.return_value = 'foo'
        self.segment_read_config.get_guc_value.return_value = 'bar'
        another_segment_read_config = Mock()
        another_segment_read_config.get_guc_value.return_value = "baz"
        another_segment_read_config.get_seg_content_id.return_value = 1
        self.pool.getCompletedItems.return_value.append(another_segment_read_config)
        self.host_cache.get_hosts.return_value.extend([self.host, self.host])

        self.subject.do_main()

        self.assertEqual(self.pool.addCommand.call_count, 3)
        self.assertEqual(self.subject.LOGGER.error.call_count, 0)
        self.assertIn("WARNING: GUCS ARE OUT OF SYNC", mock_stdout.getvalue())
        self.assertIn("bar", mock_stdout.getvalue())
        self.assertIn("[name: my_property_name] [value: baz]", mock_stdout.getvalue())

    def test_option_change_value_master_separate_succeed(self):
        db_singleton_side_effect_list.append("some happy result")
        entry = 'my_property_name'
        sys.argv = ["gpconfig", "-c", entry, "-v", "100", "-m", "20"]
        # 'SELECT name, setting, unit, short_desc, context, vartype, min_val, max_val FROM pg_settings'
        self.cursor.set_result_for_testing([['my_property_name', 'setting', 'unit', 'short_desc',
                                             'context', 'vartype', 'min_val', 'max_val']])

        self.subject.do_main()

        self.subject.LOGGER.info.assert_called_with("completed successfully")
        self.assertEqual(self.pool.addCommand.call_count, 2)
        segment_command = self.pool.addCommand.call_args_list[0][0][0]
        self.assertTrue("my_property_name" in segment_command.cmdStr)
        value = base64.urlsafe_b64encode(pickle.dumps("100"))
        self.assertTrue(value in segment_command.cmdStr)
        master_command = self.pool.addCommand.call_args_list[1][0][0]
        self.assertTrue("my_property_name" in master_command.cmdStr)
        value = base64.urlsafe_b64encode(pickle.dumps("20"))
        self.assertTrue(value in master_command.cmdStr)

    def test_option_change_value_masteronly_succeed(self):
        db_singleton_side_effect_list.append("some happy result")
        entry = 'my_property_name'
        sys.argv = ["gpconfig", "-c", entry, "-v", "100", "--masteronly"]
        # 'SELECT name, setting, unit, short_desc, context, vartype, min_val, max_val FROM pg_settings'
        self.cursor.set_result_for_testing([['my_property_name', 'setting', 'unit', 'short_desc',
                                             'context', 'vartype', 'min_val', 'max_val']])

        self.subject.do_main()

        self.subject.LOGGER.info.assert_called_with("completed successfully")
        self.assertEqual(self.pool.addCommand.call_count, 1)
        master_command = self.pool.addCommand.call_args_list[0][0][0]
        self.assertTrue(("my_property_name") in master_command.cmdStr)
        value = base64.urlsafe_b64encode(pickle.dumps("100"))
        self.assertTrue(value in master_command.cmdStr)

    def test_option_change_value_master_separate_fail_not_valid_guc(self):
        db_singleton_side_effect_list.append("DatabaseError")

        with self.assertRaisesRegexp(Exception, "not a valid GUC: my_property_name"):
            sys.argv = ["gpconfig", "-c", "my_property_name", "-v", "100", "-m", "20"]
            self.subject.do_main()

        self.assertEqual(self.subject.LOGGER.fatal.call_count, 1)

    def test_option_change_value_hidden_guc_with_skipvalidation(self):
        sys.argv = ["gpconfig", "-c", "my_hidden_guc_name", "-v", "100", "--skipvalidation"]
        self.subject.do_main()

        self.subject.LOGGER.info.assert_called_with("completed successfully")
        self.assertEqual(self.pool.addCommand.call_count, 2)
        segment_command = self.pool.addCommand.call_args_list[0][0][0]
        self.assertTrue("my_hidden_guc_name" in segment_command.cmdStr)
        master_command = self.pool.addCommand.call_args_list[1][0][0]
        self.assertTrue("my_hidden_guc_name" in master_command.cmdStr)
        value = base64.urlsafe_b64encode(pickle.dumps("100"))
        self.assertTrue(value in master_command.cmdStr)

    def test_option_change_value_hidden_guc_without_skipvalidation(self):
        db_singleton_side_effect_list.append("my happy result")

        with self.assertRaisesRegexp(Exception, "GUC Validation Failed: my_hidden_guc_name cannot be changed under "
                                                "normal conditions. Please refer to gpconfig documentation."):
            sys.argv = ["gpconfig", "-c", "my_hidden_guc_name", "-v", "100"]
            self.subject.do_main()

        self.subject.LOGGER.fatal.assert_called_once_with("GUC Validation Failed: my_hidden_guc_name cannot be "
                                                          "changed under normal conditions. "
                                                          "Please refer to gpconfig documentation.")

    @patch('sys.stdout', new_callable=StringIO)
    def test_option_file_compare_returns_same_value(self, mock_stdout):
        sys.argv = ["gpconfig", "-s", "my_property_name", "--file-compare"]
        self.master_read_config.get_guc_value.return_value = 'foo'
        self.master_read_config.get_seg_content_id.return_value = -1

        self.segment_read_config.get_guc_value.return_value = 'foo'
        self.segment_read_config.get_seg_content_id.return_value = 0

        another_segment_read_config = Mock()
        another_segment_read_config.get_guc_value.return_value = "foo"
        another_segment_read_config.get_seg_content_id.return_value = 1
        self.pool.getCompletedItems.return_value.append(another_segment_read_config)

        self.cursor.set_result_for_testing([[-1, 'my_property_name', 'foo'],
                                            [0, 'my_property_name', 'foo'],
                                            [1, 'my_property_name', 'foo']])

        self.subject.do_main()

        self.assertIn("Master  value: foo | file: foo", mock_stdout.getvalue())
        self.assertIn("Segment value: foo | file: foo", mock_stdout.getvalue())
        self.assertIn("Values on all segments are consistent", mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=StringIO)
    def test_option_file_compare_returns_different_value(self, mock_stdout):
        sys.argv = ["gpconfig", "-s", "my_property_name", "--file-compare"]
        self.master_read_config.get_guc_value.return_value = 'foo'
        self.master_read_config.get_seg_content_id.return_value = -1
        self.master_read_config.get_seg_dbid.return_value = 0

        self.segment_read_config.get_guc_value.return_value = 'foo'
        self.segment_read_config.get_seg_content_id.return_value = 0
        self.segment_read_config.get_seg_dbid.return_value = 1

        another_segment_read_config = Mock()
        another_segment_read_config.get_guc_value.return_value = "bar"
        another_segment_read_config.get_seg_content_id.return_value = 1
        another_segment_read_config.get_seg_dbid.return_value = 2
        self.pool.getCompletedItems.return_value.append(another_segment_read_config)

        self.cursor.set_result_for_testing([[-1, 'my_property_name', 'foo'],
                                            [0, 'my_property_name', 'foo'],
                                            [1, 'my_property_name', 'foo']])

        self.subject.do_main()

        self.assertIn("WARNING: GUCS ARE OUT OF SYNC: ", mock_stdout.getvalue())
        self.assertIn("[context: -1] [dbid: 0] [name: my_property_name] [value: foo | file: foo]",
                      mock_stdout.getvalue())
        self.assertIn("[context: 0] [dbid: 1] [name: my_property_name] [value: foo | file: foo]",
                      mock_stdout.getvalue())
        self.assertIn("[context: 1] [dbid: 2] [name: my_property_name] [value: foo | file: bar]",
                      mock_stdout.getvalue())

    @patch('sys.stdout', new_callable=StringIO)
    def test_option_file_compare_with_standby_master_with_different_file_value_will_report_failure(self, mock_stdout):
        sys.argv = ["gpconfig", "-s", "my_property_name", "--file-compare"]
        self.cursor.set_result_for_testing([[-1, 'my_property_name', 'foo']])
        self.master_read_config.get_guc_value.return_value = 'foo'
        self.master_read_config.get_seg_content_id.return_value = -1
        self.master_read_config.get_seg_dbid.return_value = 0
        # standby mirror with bad file value
        self.segment_read_config.get_guc_value.return_value = 'foo'
        self.segment_read_config.get_seg_content_id.return_value = 0
        self.segment_read_config.get_seg_dbid.return_value = 1

        standby_segment_read_config = Mock()
        standby_segment_read_config.get_guc_value.return_value = "bar"
        standby_segment_read_config.get_seg_content_id.return_value = -1
        standby_segment_read_config.get_seg_dbid.return_value = 2
        self.pool.getCompletedItems.return_value.append(standby_segment_read_config)

        self.subject.do_main()

        self.assertIn("WARNING: GUCS ARE OUT OF SYNC: ", mock_stdout.getvalue())
        self.assertIn("[context: -1] [dbid: 0] [name: my_property_name] [value: foo | file: foo]",
                      mock_stdout.getvalue())
        self.assertIn("[context: -1] [dbid: 2] [name: my_property_name] [value: foo | file: bar]",
                      mock_stdout.getvalue())

    @staticmethod
    def _create_gparray_with_2_primary_2_mirrors():
        master = GpDB.initFromString(
            "1|-1|p|p|s|u|mdw|mdw|5432|None|/data/master||/data/master/base/10899,/data/master/base/1,/data/master/base/10898,/data/master/base/25780,/data/master/base/34782")
        primary0 = GpDB.initFromString(
            "2|0|p|p|s|u|sdw1|sdw1|40000|41000|/data/primary0||/data/primary0/base/10899,/data/primary0/base/1,/data/primary0/base/10898,/data/primary0/base/25780,/data/primary0/base/34782")
        primary1 = GpDB.initFromString(
            "3|1|p|p|s|u|sdw2|sdw2|40001|41001|/data/primary1||/data/primary1/base/10899,/data/primary1/base/1,/data/primary1/base/10898,/data/primary1/base/25780,/data/primary1/base/34782")
        mirror0 = GpDB.initFromString(
            "4|0|m|m|s|u|sdw2|sdw2|50000|51000|/data/mirror0||/data/mirror0/base/10899,/data/mirror0/base/1,/data/mirror0/base/10898,/data/mirror0/base/25780,/data/mirror0/base/34782")
        mirror1 = GpDB.initFromString(
            "5|1|m|m|s|u|sdw1|sdw1|50001|51001|/data/mirror1||/data/mirror1/base/10899,/data/mirror1/base/1,/data/mirror1/base/10898,/data/mirror1/base/25780,/data/mirror1/base/34782")
        return GpArray([master, primary0, primary1, mirror0, mirror1])


if __name__ == '__main__':
    run_tests()
