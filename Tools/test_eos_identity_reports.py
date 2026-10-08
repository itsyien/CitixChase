import unittest
from compare_eos_identity_reports import compare_reports


def report(device, identity, **changes):
    value = dict(schema=1, success=True, auth_method="DeviceId", device_tag=device,
                 product_id="p", sandbox_id="s", deployment_id="d", puid=identity,
                 login_status="LoggedIn", engine_version="5.8.3", sdk_version="1.18",
                 runtime_kind="UnrealCommandlet")
    value.update(changes)
    return value


class IdentityReportTests(unittest.TestCase):
    def test_two_devices_and_stable_restart_pass(self):
        self.assertEqual(compare_reports(report("A", "a" * 32), report("B", "b" * 32),
                                         report("A", "a" * 32)), [])

    def test_two_processes_on_same_device_are_not_proof(self):
        self.assertTrue(compare_reports(report("A", "a" * 32), report("A", "b" * 32)))

    def test_matching_puids_fail(self):
        self.assertTrue(compare_reports(report("A", "a" * 32), report("B", "a" * 32)))

    def test_changed_identity_after_restart_fails(self):
        self.assertTrue(compare_reports(report("A", "a" * 32), report("B", "b" * 32),
                                        report("A", "c" * 32)))

    def test_different_environment_cannot_pass(self):
        self.assertTrue(compare_reports(report("A", "a" * 32),
                                        report("B", "b" * 32, deployment_id="other")))

    def test_failed_or_invalid_login_cannot_pass(self):
        for changes in ({"success": False}, {"puid": ""}, {"login_status": "NotLoggedIn"},
                        {"auth_method": "Epic"}, {"engine_version": "5.7"}, {"schema": 99},
                        {"runtime_kind": "Unknown"}):
            with self.subTest(changes=changes):
                self.assertTrue(compare_reports(report("A", "a" * 32),
                                                report("B", "b" * 32, **changes)))


if __name__ == "__main__":
    unittest.main()
