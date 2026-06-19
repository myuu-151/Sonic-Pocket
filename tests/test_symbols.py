import csv
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = ROOT / "analysis" / "symbols.csv"


class SymbolDatabaseTests(unittest.TestCase):
    def test_schema_and_values(self):
        with SYMBOLS.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            self.assertEqual(
                reader.fieldnames,
                ["address", "name", "kind", "confidence", "notes"],
            )
            rows = list(reader)

        self.assertGreater(len(rows), 0)
        addresses = set()
        names = set()
        for row in rows:
            address = int(row["address"], 16)
            self.assertNotIn(address, addresses)
            self.assertNotIn(row["name"], names)
            self.assertIn(row["kind"], {"function", "data"})
            self.assertIn(row["confidence"], {"confirmed", "probable", "unknown"})
            self.assertTrue(row["notes"])
            addresses.add(address)
            names.add(row["name"])


if __name__ == "__main__":
    unittest.main()
