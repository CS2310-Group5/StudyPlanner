"""Offline regression tests for the Excel writer; no student data or network."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from openpyxl import load_workbook

WRITER = Path(__file__).resolve().parents[1] / 'excel_writer.py'

class WorkbookTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        plan = {'plan_years': 2, 'courses': [
            {'code':'TEST100', 'title':'Year-long project', 'place':'Year 1 Semester B', 'term_credits':3, 'second_place':'Year 2 Semester A', 'second_term_credits':3, 'credits':6},
            {'code':'TEST200', 'title':'In progress', 'place':'Year 1 Semester A', 'term_credits':3, 'requirement':'In progress', 'prerequisite_statuses':{'TEST999':'not listed'}, 'counts_toward_degree':False},
            {'code':'TEST300', 'title':'Manual course', 'manual_placement':True, 'credits':3},
            {'code':'TEST400', 'title':'Missing prerequisite', 'place':'Year 2 Semester B', 'term_credits':3, 'prerequisite_statuses':{'TEST999':'not listed', 'TEST100':'planned'}}]}
        source = root/'plan.json'; source.write_text(json.dumps(plan))
        output = root/'plan.xlsx'
        subprocess.run([sys.executable, str(WRITER), '--plan', str(source), '--output', str(output)], check=True)
        self.book = load_workbook(output, data_only=True)
        self.addCleanup(self.book.close)
        self.sheet = self.book['Study Schedule']

    def test_split_course_and_totals(self):
        rows = list(self.sheet.values)
        self.assertEqual(sum(row.count('TEST100') for row in rows), 2)
        metadata = {row[0]:row[1] for row in rows if row[0]}
        self.assertEqual(metadata['Total scheduled credits'], 12)
        self.assertEqual(metadata['Degree credits planned'], 9)
        self.assertIn('Year 1 total: 6 credits', metadata)
        self.assertIn('Year 2 total: 6 credits', metadata)

    def test_prerequisite_remarks(self):
        for row in self.sheet.iter_rows():
            for cell in row:
                if cell.value == 'TEST200':
                    self.assertIn(self.sheet.cell(cell.row, cell.column+3).value, (None, ''))
                if cell.value == 'TEST400':
                    remark = self.sheet.cell(cell.row, cell.column+3)
                    self.assertIn('TEST999', remark.value)
                    self.assertNotIn('TEST100', remark.value)
                    self.assertEqual(remark.fill.fgColor.rgb[-6:], 'FFF2CC')

    def test_manual_and_layout(self):
        self.assertEqual(self.book['Manual Placement']['A2'].value, 'TEST300')
        self.assertEqual(self.book['Manual Placement'].freeze_panes, 'A2')
        self.assertEqual(self.sheet.page_setup.fitToWidth, 1)
        self.assertEqual(len(self.book.sheetnames), 4)

    def test_legacy_summer_courses_are_preserved_for_manual_placement(self):
        root = Path(self.temp.name)
        source = root / "summer-plan.json"
        output = root / "summer-plan.xlsx"
        source.write_text(json.dumps({"plan_years": 2, "courses": [
            {"code": "REG1001", "title": "Regular", "credits": 3,
             "place": "Year 1 Semester B", "term_credits": 3},
            {"code": "SUM1001", "title": "Summer placement", "credits": 3,
             "place": "Year 2 Summer", "term_credits": 3},
            {"code": "SPL1001", "title": "Split course", "credits": 6,
             "place": "Year 2 Semester B", "term_credits": 3,
             "second_place": "Year 2 Summer", "second_term_credits": 3}
        ]}))
        subprocess.run([sys.executable, str(WRITER), "--plan", str(source),
                        "--output", str(output)], check=True)
        book = load_workbook(output, data_only=True)
        try:
            schedule = list(book["Study Schedule"].values)
            metadata = {row[0]: row[1] for row in schedule if row[0]}
            self.assertEqual(metadata["Total scheduled credits"], 3)
            for code in ("SUM1001", "SPL1001"):
                self.assertFalse(any(code in row for row in schedule))
            manual = list(book["Manual Placement"].values)
            self.assertEqual([(row[0], row[2]) for row in manual[1:]],
                             [("SUM1001", 3), ("SPL1001", 6)])
            self.assertTrue(all("automatic placement included Summer" in row[3]
                                for row in manual[1:]))
            notes = " ".join(str(value) for row in book["Notes"].values for value in row)
            self.assertIn("automatic placement included Summer", notes)
            self.assertEqual(sum(row.count("REG1001") for row in schedule), 1)
        finally:
            book.close()

    def test_invalid_semester_still_fails(self):
        root = Path(self.temp.name)
        source = root / "invalid-plan.json"
        source.write_text(json.dumps({"plan_years": 1, "courses": [
            {"code": "BAD1001", "credits": 3, "place": "Year 8 Semester A", "term_credits": 3}
        ]}))
        result = subprocess.run([sys.executable, str(WRITER), "--plan", str(source),
                                 "--output", str(root / "invalid.xlsx")],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid semester", result.stderr)

if __name__ == '__main__':
    unittest.main()
