# StudyPlanner
Scrapes CityU course offerings and requirements, then computes a semester-by-semester study plan exported as Excel.

## Preview the course catalogue

Requires a C++17 compiler. From the repository root, build and run:

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic main.cpp -o study-planner.exe
.\study-planner.exe
```

The preview reads `data/courses.csv` and prints each course, its credits, academic
year, listed semesters, and precursor options. Pass a file path as the first
argument to preview another catalogue with the same five columns. The sample uses
`|` between offered semesters or between alternative precursor courses.
