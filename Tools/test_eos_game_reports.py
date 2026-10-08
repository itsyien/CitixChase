import copy
import unittest
from compare_eos_game_reports import compare


class GameReportTests(unittest.TestCase):
    def setUp(self):
        self.host = dict(success=True, replicated_match=True, action="Host", players=2,
                         connection_type="Relayed", eos_listener=True, puid="a" * 32,
                         remote_puid="b" * 32, session_id="session", city_seed=45, city_hash=76)
        self.guest = copy.deepcopy(self.host)
        self.guest.update(action="Join", puid="b" * 32, remote_puid="a" * 32)

    def test_matching_real_game_shape(self):
        self.assertEqual(compare(self.host, self.guest), [])

    def test_same_identity_cannot_establish_multiplayer(self):
        self.guest["puid"] = self.host["puid"]
        self.assertTrue(compare(self.host, self.guest))

    def test_direct_connection_cannot_prove_relay(self):
        self.guest["connection_type"] = "Direct"
        self.assertTrue(compare(self.host, self.guest))

    def test_search_only_receipt_cannot_prove_gameplay(self):
        self.guest.update(action="Search", replicated_match=False)
        self.assertTrue(compare(self.host, self.guest))

    def test_city_or_session_mismatch_is_rejected(self):
        for key in ("session_id", "city_hash", "city_seed"):
            guest = dict(self.guest, **{key: "different"})
            self.assertTrue(compare(self.host, guest))

    def test_wrong_transport_peer_is_rejected(self):
        self.host["remote_puid"] = "c" * 32
        self.assertTrue(compare(self.host, self.guest))
