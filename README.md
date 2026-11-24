# CityU Automated Study Plan Generator

A C++17 command-line application that builds a customizable Excel study plan from Degree Works information and shared CityU course-catalogue data.

The application reads Degree Works audit information, retrieves or imports catalogue records, lets users select elective courses, schedules courses across academic terms, and generates a structured Excel workbook for review.

## Course and Team Members

**Course:** CS2310  
**Group:** 5

| No. | Name | Responsibilities |
| --- | --- | --- |
| 1 | CHIU Wai Yan | Development of the Excel export module using Python and `openpyxl`, including conversion of the generated plan into a structured, readable workbook. |
| 3 | LEE Hei Tung | Degree Works and course scanner testing, feature validation, troubleshooting, and workflow verification. |
| 4 | WONG Cheuk Ying | |
| 5 | WONG Lai Lam | Initial implementation of the study planner, including the C++ program skeleton, Degree Works course retrieval, elective selection workflow, Excel output integration, and course-website scraping support. |

## Table of Contents

1. [Overview](#overview)
2. [Features](#features)
3. [Requirements](#requirements)
4. [Build](#build)
5. [Run](#run)
6. [Command-Line Options](#command-line-options)
7. [Course Database](#course-database)
8. [Degree Works Authentication](#degree-works-authentication)
9. [Elective Selection and Scheduling](#elective-selection-and-scheduling)
10. [Excel Workbook](#excel-workbook)
11. [Privacy and Data Storage](#privacy-and-data-storage)
12. [Commands and Interactive Controls](#commands-and-interactive-controls)
13. [Error Codes and Logs](#error-codes-and-logs)
14. [Database Schema](#database-schema)
15. [Overrides](#overrides)
16. [Testing](#testing)
17. [Limitations](#limitations)

## Overview

The study-plan generator combines:

- Student and programme information from a Degree Works audit.
- Completed, in-progress, and required courses.
- Elective requirements and area requirements.
- Information from a local course catalogue database.
- Optional updates obtained from official CityU course pages.
- Scheduling and elective-ranking rules.
- An Excel export generated through a Python `openpyxl` module.

The application does not confirm the correctness of a Degree Works audit or replace academic advising. It produces a planning aid that should be checked against official records and individual programme requirements.

## Features

- Reads saved Degree Works audit text and extracts course codes and audit metadata.
- Supports manual SAML/Okta login through a visible Playwright browser.
- Imports copied CityU catalogue text.
- Fetches and caches course-page information.
- Stores successful course updates locally.
- Handles missing course details without silently inventing verification data.
- Groups multiple elective requirements and area requirements.
- Ranks electives according to prerequisite readiness and optional user preferences.
- Places in-progress courses in their reported semesters.
- Spreads required courses across Semester A and Semester B.
- Keeps Summer terms available for manual planning.
- Retains courses that cannot be scheduled safely on a Manual Placement worksheet.
- Exports the completed plan as an Excel workbook.
- Provides clickable course links and per-course warning remarks.

## Requirements

- CMake 3.20 or newer
- A C++17 compiler
- libcurl
- Python 3
- `openpyxl`
- Playwright and Chromium only for live Degree Works login or live catalogue browsing

### Debian/Ubuntu

```bash
sudo apt install cmake g++ libcurl4-openssl-dev python3 python3-pip python3-venv libnspr4 libnss3