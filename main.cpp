#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <fstream>
#include <algorithm>
#define RESET "\033[0m"
#define CYAN "\033[36m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define RED "\033[31m"

#include <mysqlx/xdevapi.h>

class Person
{
protected:
    std::string id;
    std::string name;
    std::string email;

public:
    Person(const std::string& id, const std::string& name, const std::string& email)
        : id(id), name(name), email(email)
    {
    }
    virtual void menu() = 0;
    virtual std::string getRole() const = 0;
    virtual ~Person() {}
    std::string getId() const { return id; }
    std::string getName() const { return name; }
    std::string getEmail() const { return email; }
};

class Database {
    mysqlx::Session session;
    mysqlx::Schema db;

public:
    Database(const std::string& host, const std::string& user, const std::string& pass, const std::string& dbname)
        try : session(mysqlx::SessionOption::HOST, host,
                     mysqlx::SessionOption::PORT, 33060,
                     mysqlx::SessionOption::USER, user,
                     mysqlx::SessionOption::PWD, pass,
                     mysqlx::SessionOption::DB, dbname),
              db(session.getSchema(dbname))
    {
        try {
            db = session.getSchema(dbname);
            if (!db.existsInDatabase()) {
                throw std::runtime_error("Database " + dbname + " does not exist");
            }
        }
        catch (const mysqlx::Error& err) {
            throw std::runtime_error("Failed to get schema: " + std::string(err.what()));
        }
    }
    catch (const mysqlx::Error& err) {
        throw std::runtime_error("Connection failed: " + std::string(err.what()));
    }

    ~Database() {
        try {
            session.close();
        } catch (...) {}
    }

    bool studentExists(const std::string& studentId)
    {
        auto students = db.getTable("students");
        auto res = students.select("COUNT(*)").where("student_id = :sid").bind("sid", studentId).execute();
        auto row = res.fetchOne();
        return row && row[0].get<int>() > 0;
    }
    bool validateStudentPassword(const std::string& studentId, const std::string& password)
    {
        auto students = db.getTable("students");
        auto res = students.select("password").where("student_id = :sid").bind("sid", studentId).execute();
        auto row = res.fetchOne();
        return row && row[0].get<std::string>() == password;
    }
    bool changeStudentPassword(const std::string& studentId, const std::string& newPassword)
    {
        auto students = db.getTable("students");
        auto res = students.update().set("password", newPassword).where("student_id = :sid").bind("sid", studentId).execute();
        return res.getAffectedItemsCount() > 0;
    }
    bool resetStudentPassword(const std::string& studentId)
    {
        return changeStudentPassword(studentId, "bnu");
    }
    int getStudentSemester(const std::string& studentId)
    {
        auto students = db.getTable("students");
        auto res = students.select("semester").where("student_id = :sid").bind("sid", studentId).execute();
        auto row = res.fetchOne();
        return row ? row[0].get<int>() : -1;
    }
    std::string getStudentDegree(const std::string& studentId)
    {
        auto students = db.getTable("students");
        auto res = students.select("degree").where("student_id = :sid").bind("sid", studentId).execute();
        auto row = res.fetchOne();
        return row ? std::string(row[0].get<std::string>()) : "";
    }

    // Faculty related methods
    bool facultyExists(const std::string& email)
    {
        auto faculty = db.getTable("faculty");
        auto res = faculty.select("COUNT(*)").where("email = :email").bind("email", email).execute();
        auto row = res.fetchOne();
        return row && row[0].get<int>() > 0;
    }

    bool validateFacultyPassword(const std::string& email, const std::string& password)
    {
        auto faculty = db.getTable("faculty");
        auto res = faculty.select("password").where("email = :email").bind("email", email).execute();
        auto row = res.fetchOne();
        return row && row[0].get<std::string>() == password;
    }

    std::string getFacultyId(const std::string& email)
    {
        auto faculty = db.getTable("faculty");
        auto res = faculty.select("faculty_id").where("email = :email").bind("email", email).execute();
        auto row = res.fetchOne();
        return row ? std::to_string(row[0].get<int>()) : "";
    }

    std::string getFacultyName(const std::string& email)
    {
        auto faculty = db.getTable("faculty");
        auto res = faculty.select("first_name", "last_name").where("email = :email").bind("email", email).execute();
        auto row = res.fetchOne();
        return row ? (row[0].get<std::string>() + " " + row[1].get<std::string>()) : "";
    }

    bool changeFacultyPassword(const std::string& email, const std::string& newPassword)
    {
        auto faculty = db.getTable("faculty");
        auto res = faculty.update().set("password", newPassword).where("email = :email").bind("email", email).execute();
        return res.getAffectedItemsCount() > 0;
    }

    bool resetFacultyPassword(const std::string& email)
    {
        return changeFacultyPassword(email, "faculty_scit");
    }

    struct ScheduledCourse
    {
        int schedule_id;
        std::string course_code, course_name, department;
        int semester, faculty_id, timeslot_id;
        std::string faculty_name, day, start_time, end_time;
        std::string room_id, room_number, building;
    };

    std::vector<ScheduledCourse> getAvailableScheduledCourses(int semester, const std::string& degree)
    {
        std::vector<ScheduledCourse> result;
        std::string query =
            "SELECT cs.schedule_id, cs.course_code, c.course_name, f.first_name, f.last_name, "
            "t.day_of_week, CAST(t.start_time AS CHAR), CAST(t.end_time AS CHAR), "
            "cl.room_number, cl.building, cs.timeslot_id "
            "FROM course_schedule cs "
            "JOIN courses c ON cs.course_code = c.course_code "
            "JOIN faculty f ON cs.faculty_id = f.faculty_id "
            "JOIN timeslots t ON cs.timeslot_id = t.timeslot_id "
            "JOIN classrooms cl ON cs.room_id = cl.room_id "
            "WHERE c.semester = ? AND c.department = ?";

        auto res = session.sql(query).bind(semester, degree).execute();
        mysqlx::Row row;

        while ((row = res.fetchOne())) {
            ScheduledCourse sc;
            sc.schedule_id   = row[0].get<int>();
            sc.course_code   = row[1].get<std::string>();
            sc.course_name   = row[2].get<std::string>();
            sc.faculty_name  = row[3].get<std::string>() + " " + row[4].get<std::string>();
            sc.day           = row[5].get<std::string>();
            sc.start_time    = row[6].get<std::string>();
            sc.end_time      = row[7].get<std::string>();
            sc.room_number   = row[8].get<std::string>();
            sc.building      = row[9].get<std::string>();
            sc.timeslot_id   = row[10].get<int>();

            result.push_back(sc);
        }
        return result;
    }

    bool isAlreadyEnrolled(const std::string& studentId, int schedule_id)
    {
        auto enrollments = db.getTable("enrollments");
        auto res = enrollments.select("COUNT(*)")
            .where("student_id = :sid AND schedule_id = :scid")
            .bind("sid", studentId)
            .bind("scid", schedule_id)
            .execute();
        auto row = res.fetchOne();
        return row && row[0].get<int>() > 0;
    }
    bool hasClash(const std::string& studentId, int timeslot_id)
    {
        std::string query =
            "SELECT COUNT(*) FROM enrollments e "
            "JOIN course_schedule cs ON e.schedule_id = cs.schedule_id "
            "WHERE e.student_id = ? AND cs.timeslot_id = ?";
        auto res = session.sql(query).bind(studentId, timeslot_id).execute();
        auto row = res.fetchOne();
        return row && row[0].get<int>() > 0;
    }
    bool addEnrollment(const std::string& studentId, int schedule_id)
    {
        std::string course_code;
        {
            std::string query = "SELECT course_code FROM course_schedule WHERE schedule_id = ?";
            auto res = session.sql(query).bind(schedule_id).execute();
            auto row = res.fetchOne();
            if (!row) return false;
            course_code = row[0].get<std::string>();
        }
        int max_students = 0;
        {
            std::string query = "SELECT max_students FROM courses WHERE course_code = ?";
            auto res = session.sql(query).bind(course_code).execute();
            auto row = res.fetchOne();
            if (!row) return false;
            max_students = row[0].get<int>();
        }
        int enrolled = 0;
        {
            std::string query = "SELECT COUNT(*) FROM enrollments WHERE schedule_id = ?";
            auto res = session.sql(query).bind(schedule_id).execute();
            auto row = res.fetchOne();
            if (!row) return false;
            enrolled = row[0].get<int>();
        }
        if (enrolled >= max_students)
            return false;
        auto enrollments = db.getTable("enrollments");
        enrollments.insert("student_id", "schedule_id").values(studentId, schedule_id).execute();
        return true;
    }
    bool dropEnrollment(const std::string& studentId, int schedule_id)
    {
        auto enrollments = db.getTable("enrollments");
        auto res = enrollments.remove()
            .where("student_id = :sid AND schedule_id = :scid")
            .bind("sid", studentId)
            .bind("scid", schedule_id)
            .execute();
        return res.getAffectedItemsCount() > 0;
    }
    std::vector<ScheduledCourse> getEnrolledCourses(const std::string& studentId)
    {
        std::vector<ScheduledCourse> result;
        std::string query =
            "SELECT cs.schedule_id, c.course_code, c.course_name, c.department, c.semester, "
            "f.faculty_id, CONCAT(f.first_name,' ',f.last_name) AS faculty_name, "
            "t.timeslot_id, t.day_of_week, CAST(t.start_time AS CHAR), CAST(t.end_time AS CHAR), "
            "cl.room_id, cl.room_number, cl.building "
            "FROM enrollments e "
            "JOIN course_schedule cs ON e.schedule_id = cs.schedule_id "
            "JOIN courses c ON cs.course_code = c.course_code "
            "JOIN faculty f ON cs.faculty_id = f.faculty_id "
            "JOIN timeslots t ON cs.timeslot_id = t.timeslot_id "
            "JOIN classrooms cl ON cs.room_id = cl.room_id "
            "WHERE e.student_id = ?";

        auto res = session.sql(query).bind(studentId).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
        {
            result.push_back({
                row[0].get<int>(),
                row[1].get<std::string>(),
                row[2].get<std::string>(),
                row[3].get<std::string>(),
                row[4].get<int>(),
                row[5].get<int>(),
                row[7].get<int>(),
                row[6].get<std::string>(),
                row[8].get<std::string>(),
                row[9].get<std::string>(),
                row[10].get<std::string>(),
                row[11].get<std::string>(),
                row[12].get<std::string>(),
                row[13].get<std::string>()
            });
        }
        return result;
    }

    typedef ScheduledCourse TimetableEntry;
    std::vector<TimetableEntry> getStudentTimetable(const std::string& studentId)
    {
        return getEnrolledCourses(studentId);
    }

    // Faculty specific methods
    std::vector<std::string> getFacultyCourses(int facultyId)
    {
        std::vector<std::string> result;
        std::string query =
            "SELECT DISTINCT cs.course_code, c.course_name FROM course_schedule cs "
            "JOIN courses c ON cs.course_code = c.course_code "
            "WHERE cs.faculty_id = ?";
        auto res = session.sql(query).bind(facultyId).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
        {
            result.push_back(row[0].get<std::string>() + " - " + row[1].get<std::string>());
        }
        return result;
    }

    struct StudentInfo
    {
        std::string student_id;
        std::string first_name;
        std::string last_name;
        std::string email;
        int semester;
        std::string degree;
    };

    std::vector<StudentInfo> getEnrolledStudentsInCourse(const std::string& course_code)
    {
        std::vector<StudentInfo> result;
        std::string query =
            "SELECT DISTINCT s.student_id, s.first_name, s.last_name, s.email, s.semester, s.degree "
            "FROM enrollments e "
            "JOIN students s ON e.student_id = s.student_id "
            "JOIN course_schedule cs ON e.schedule_id = cs.schedule_id "
            "WHERE cs.course_code = ?";
        auto res = session.sql(query).bind(course_code).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
        {
            StudentInfo si;
            si.student_id = row[0].get<std::string>();
            si.first_name = row[1].get<std::string>();
            si.last_name = row[2].get<std::string>();
            si.email = row[3].get<std::string>();
            si.semester = row[4].get<int>();
            si.degree = row[5].get<std::string>();
            result.push_back(si);
        }
        return result;
    }

    std::vector<ScheduledCourse> getFacultyTimetable(int facultyId)
    {
        std::vector<ScheduledCourse> result;
        std::string query =
            "SELECT cs.schedule_id, cs.course_code, c.course_name, c.department, c.semester, "
            "cs.faculty_id, cs.timeslot_id, CONCAT(f.first_name, ' ', f.last_name) AS faculty_name, "
            "t.day_of_week, CAST(t.start_time AS CHAR), CAST(t.end_time AS CHAR), "
            "cs.room_id, cl.room_number, cl.building "
            "FROM course_schedule cs "
            "JOIN courses c ON cs.course_code = c.course_code "
            "JOIN faculty f ON cs.faculty_id = f.faculty_id "
            "JOIN timeslots t ON cs.timeslot_id = t.timeslot_id "
            "JOIN classrooms cl ON cs.room_id = cl.room_id "
            "WHERE cs.faculty_id = ?";
        auto res = session.sql(query).bind(facultyId).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
        {
            ScheduledCourse sc;
            sc.schedule_id = row[0].get<int>();
            sc.course_code = row[1].get<std::string>();
            sc.course_name = row[2].get<std::string>();
            sc.department = row[3].get<std::string>();
            sc.semester = row[4].get<int>();
            sc.faculty_id = row[5].get<int>();
            sc.timeslot_id = row[6].get<int>();
            sc.faculty_name = row[7].get<std::string>();
            sc.day = row[8].get<std::string>();
            sc.start_time = row[9].get<std::string>();
            sc.end_time = row[10].get<std::string>();
            sc.room_id = row[11].get<std::string>();
            sc.room_number = row[12].get<std::string>();
            sc.building = row[13].get<std::string>();
            result.push_back(sc);
        }
        return result;
    }

    void addMarks(const std::string& course_code, const std::string& student_id, const std::string& assignment_name, int total_marks, int obtained_marks)
    {
        try {
            std::string query = "INSERT INTO marks (course_code, student_id, assignment_name, total_marks, obtained_marks) VALUES (?, ?, ?, ?, ?) "
                                "ON DUPLICATE KEY UPDATE total_marks = VALUES(total_marks), obtained_marks = VALUES(obtained_marks)";
            session.sql(query).bind(course_code, student_id, assignment_name, total_marks, obtained_marks).execute();
        }
        catch (const mysqlx::Error& err) {
            std::cout << "Error adding marks: " << err.what() << std::endl;
        }
    }

    void updateMarks(const std::string& course_code, const std::string& student_id, const std::string& assignment_name, int obtained_marks)
    {
        try {
            std::string query = "UPDATE marks SET obtained_marks = ? WHERE course_code = ? AND student_id = ? AND assignment_name = ?";
            session.sql(query).bind(obtained_marks, course_code, student_id, assignment_name).execute();
        }
        catch (const mysqlx::Error& err) {
            std::cout << "Error updating marks: " << err.what() << std::endl;
        }
    }

    std::vector<std::string> getAssignmentsForCourse(const std::string& course_code) {
        std::vector<std::string> assignments;
        std::string query = "SELECT DISTINCT assignment_name FROM marks WHERE course_code = ?";
        auto res = session.sql(query).bind(course_code).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne())) {
            assignments.push_back(row[0].get<std::string>());
        }
        return assignments;
    }

    std::vector<std::pair<std::string, std::pair<int, int>>> getStudentMarksForAssignment(const std::string& course_code, const std::string& assignment_name) {
        std::vector<std::pair<std::string, std::pair<int, int>>> marks;
        std::string query = "SELECT student_id, total_marks, obtained_marks FROM marks WHERE course_code = ? AND assignment_name = ?";
        auto res = session.sql(query).bind(course_code, assignment_name).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne())) {
            marks.emplace_back(row[0].get<std::string>(), std::make_pair(row[1].get<int>(), row[2].get<int>()));
        }
        return marks;
    }

    int getTotalEnrolledStudents(const std::string& course_code)
    {
        std::string query =
            "SELECT COUNT(DISTINCT e.student_id) FROM enrollments e "
            "JOIN course_schedule cs ON e.schedule_id = cs.schedule_id "
            "WHERE cs.course_code = ?";
        auto res = session.sql(query).bind(course_code).execute();
        auto row = res.fetchOne();
        return row ? row[0].get<int>() : 0;
    }

    int getNextFacultyId()
    {
        std::string query = "SELECT MAX(faculty_id) FROM faculty";
        auto res = session.sql(query).execute();
        auto row = res.fetchOne();
        int nextId = 1;
        if (row && !row[0].isNull())
        {
            nextId = row[0].get<int>() + 1;
        }
        return nextId;
    }
    void addStudent(const std::string& id, const std::string& fname, const std::string& lname, const std::string& email, const std::string& degree, int semester)
    {
        auto students = db.getTable("students");
        students.insert("student_id", "first_name", "last_name", "email", "degree", "semester", "password")
            .values(id, fname, lname, email, degree, semester, "bnu") // password defaults to "bnu"
            .execute();
    }
    void removeStudent(const std::string& id)
    {
        auto students = db.getTable("students");
        students.remove().where("student_id = :sid").bind("sid", id).execute();
    }
    void addFaculty(int faculty_id, const std::string& fname, const std::string& lname, const std::string& email, const std::string& degree, const std::string& qualification, const std::string& expertise_sub, const std::string& designation)
    {
        auto faculty = db.getTable("faculty");
        faculty.insert("faculty_id", "first_name", "last_name", "email", "degree", "qualification", "expertise_sub", "designation", "password")
            .values(faculty_id, fname, lname, email, degree, qualification, expertise_sub, designation, "faculty_scit")
            .execute();
    }
    void removeFaculty(int faculty_id)
    {
        auto faculty = db.getTable("faculty");
        faculty.remove().where("faculty_id = :fid").bind("fid", faculty_id).execute();
    }
    void addCourse(const std::string& code, const std::string& name, int credits, int sem, const std::string& dept, int max, const std::string& prereq)
    {
        auto courses = db.getTable("courses");
        courses.insert("course_code", "course_name", "credits", "semester", "department", "max_students", "prerequisites")
            .values(code, name, credits, sem, dept, max, prereq)
            .execute();
    }
    void removeCourse(const std::string& code)
    {
        auto courses = db.getTable("courses");
        courses.remove().where("course_code = :ccode").bind("ccode", code).execute();
    }
    void addClassroom(const std::string& id, const std::string& building, const std::string& number, int capacity, const std::string& room_type)
    {
        auto classrooms = db.getTable("classrooms");
        classrooms.insert("room_id", "building", "room_number", "capacity", "room_type")
            .values(id, building, number, capacity, room_type)
            .execute();
    }
    void removeClassroom(const std::string& id)
    {
        auto classrooms = db.getTable("classrooms");
        classrooms.remove().where("room_id = :rid").bind("rid", id).execute();
    }
    void addTimeslot(const std::string& day, const std::string& start, const std::string& end)
    {
        auto timeslots = db.getTable("timeslots");
        timeslots.insert("day_of_week", "start_time", "end_time")
            .values(day, start, end)
            .execute();
    }
    void removeTimeslot(int timeslot_id)
    {
        auto timeslots = db.getTable("timeslots");
        timeslots.remove().where("timeslot_id = :tid").bind("tid", timeslot_id).execute();
    }
    std::vector<std::pair<std::string, std::string>> getUnscheduledCourses()
    {
        std::vector<std::pair<std::string, std::string>> resvec;
        std::string query =
            "SELECT course_code, course_name FROM courses WHERE course_code NOT IN (SELECT course_code FROM course_schedule)";
        auto res = session.sql(query).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
            resvec.emplace_back(row[0].get<std::string>(), row[1].get<std::string>());
        return resvec;
    }
    std::vector<std::pair<int, std::string>> getAllTimeslots()
    {
        std::vector<std::pair<int, std::string>> resvec;
        std::string query =
            "SELECT timeslot_id, CONCAT(day_of_week, ' ', start_time, '-', end_time) FROM timeslots";
        auto res = session.sql(query).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
            resvec.emplace_back(row[0].get<int>(), row[1].get<std::string>());
        return resvec;
    }
    std::vector<std::pair<std::string, std::string>> getAvailableRooms(int timeslot_id)
    {
        std::vector<std::pair<std::string, std::string>> resvec;
        std::string query =
            "SELECT room_id, CONCAT(room_number, ' ', building) FROM classrooms "
            "WHERE room_id NOT IN (SELECT room_id FROM course_schedule WHERE timeslot_id = ?)";
        auto res = session.sql(query).bind(timeslot_id).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
            resvec.emplace_back(row[0].get<std::string>(), row[1].get<std::string>());
        return resvec;
    }
    std::vector<std::pair<int, std::string>> getAvailableFaculty(int timeslot_id)
    {
        std::vector<std::pair<int, std::string>> resvec;
        std::string query =
            "SELECT faculty_id, CONCAT(first_name, ' ', last_name) FROM faculty "
            "WHERE faculty_id NOT IN (SELECT faculty_id FROM course_schedule WHERE timeslot_id = ?)";
        auto res = session.sql(query).bind(timeslot_id).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
            resvec.emplace_back(row[0].get<int>(), row[1].get<std::string>());
        return resvec;
    }
    void addCourseSchedule(const std::string& course_code, int faculty_id, int timeslot_id, const std::string& room_id)
    {
        auto course_schedule = db.getTable("course_schedule");
        course_schedule.insert("course_code", "faculty_id", "timeslot_id", "room_id")
            .values(course_code, faculty_id, timeslot_id, room_id)
            .execute();
    }
    struct ScheduledAssignment
    {
        int schedule_id;
        std::string course_code, course_name, faculty_name, room, timeslot;
    };
    std::vector<ScheduledAssignment> getAllCourseSchedules()
    {
        std::vector<ScheduledAssignment> result;
        std::string query =
            "SELECT cs.schedule_id, cs.course_code, c.course_name, CONCAT(f.first_name, ' ', f.last_name) AS faculty, "
            "CONCAT(cl.room_number, ' ', cl.building) AS room, CONCAT(t.day_of_week, ' ', t.start_time, '-', t.end_time) AS timeslot "
            "FROM course_schedule cs "
            "JOIN courses c ON cs.course_code = c.course_code "
            "JOIN faculty f ON cs.faculty_id = f.faculty_id "
            "JOIN timeslots t ON cs.timeslot_id = t.timeslot_id "
            "JOIN classrooms cl ON cs.room_id = cl.room_id";
        auto res = session.sql(query).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
        {
            result.push_back({
                row[0].get<int>(),
                row[1].get<std::string>(),
                row[2].get<std::string>(),
                row[3].get<std::string>(),
                row[4].get<std::string>(),
                row[5].get<std::string>()
            });
        }
        return result;
    }
    void removeCourseSchedule(int schedule_id)
    {
        {
            auto enrollments = db.getTable("enrollments");
            enrollments.remove().where("schedule_id = :sid").bind("sid", schedule_id).execute();
        }
        {
            auto course_schedule = db.getTable("course_schedule");
            course_schedule.remove().where("schedule_id = :sid").bind("sid", schedule_id).execute();
        }
    }
    bool isAdminPasswordCorrect(const std::string& password)
    {
        return password == "admin123";
    }

    // Marks related methods
    struct Mark {
        std::string assignment_name;
        int total_marks;
        int obtained_marks;
        std::string course_name;
    };
    std::vector<Mark> getStudentMarks(const std::string& student_id, const std::string& course_code = "")
    {
        std::vector<Mark> result;
        std::string query =
            "SELECT m.assignment_name, m.total_marks, m.obtained_marks, c.course_name "
            "FROM marks m "
            "JOIN courses c ON m.course_code = c.course_code "
            "WHERE m.student_id = ?";

        if (!course_code.empty()) {
            query += " AND m.course_code = ?";
        }

        query += " ORDER BY m.assignment_name";
        mysqlx::SqlStatement stmt = session.sql(query).bind(student_id);
        if (!course_code.empty()) {
            stmt.bind(course_code);
        }
        auto res = stmt.execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
        {
            Mark mark;
            mark.assignment_name = row[0].get<std::string>();
            mark.total_marks = row[1].get<int>();
            mark.obtained_marks = row[2].get<int>();
            mark.course_name = row[3].get<std::string>();
            result.push_back(mark);
        }
        return result;
    }
    std::vector<std::string> getStudentCourses(const std::string& student_id)
    {
        std::vector<std::string> result;
        std::string query =
            "SELECT DISTINCT c.course_code, c.course_name "
            "FROM enrollments e "
            "JOIN course_schedule cs ON e.schedule_id = cs.schedule_id "
            "JOIN courses c ON cs.course_code = c.course_code "
            "WHERE e.student_id = ?";

        auto res = session.sql(query).bind(student_id).execute();
        mysqlx::Row row;
        while ((row = res.fetchOne()))
        {
            result.push_back(row[0].get<std::string>() + " - " + row[1].get<std::string>());
        }
        return result;
    }
};

class Student : public Person
{
    Database& db;

public:
    Student(Database& db, const std::string& id, const std::string& name, const std::string& email)
        : Person(id, name, email), db(db)
    {}
    void menu() override
    {
        int choice;
        do
        {
            std::cout << CYAN << "\n--- Student Menu ---\n" << RESET;
            std::cout << "1. Add Course\n";
            std::cout << "2. Drop Course\n";
            std::cout << "3. View Timetable\n";
            std::cout << "4. View Teachers\n";
            std::cout << "5. View Classroom Details\n";
            std::cout << "6. Export Timetable\n";
            std::cout << "7. Change Password\n";
            std::cout << "8. View Marks\n";
            std::cout << "0. Logout\n";
            std::cout << "Choice: ";
            std::cin >> choice;
            switch (choice)
            {
            case 1:
                addCourse();
                break;
            case 2:
                dropCourse();
                break;
            case 3:
                viewTimetable();
                break;
            case 4:
                viewTeachers();
                break;
            case 5:
                viewClassroomDetails();
                break;
            case 6:
                exportTimetable();
                break;
            case 7:
                changePassword();
                break;
            case 8:
                viewMarks();
                break;
            case 0:
                std::cout << "Logging out...\n";
                break;
            default:
                std::cout << "Invalid choice.\n";
            }
        } while (choice != 0);
    }
    std::string getRole() const override { return "Student"; }
    void addCourse()
    {
        int sem = db.getStudentSemester(id);
        std::string deg = db.getStudentDegree(id);
        auto courses = db.getAvailableScheduledCourses(sem, deg);
        if (courses.empty())
        {
            std::cout << "No scheduled courses for your degree/semester.\n";
            return;
        }
        std::cout << "Available scheduled courses:\n";
        for (size_t i = 0; i < courses.size(); ++i)
            std::cout << i + 1 << ". " << courses[i].course_code << " - " << courses[i].course_name
            << " | " << courses[i].faculty_name << " | " << courses[i].day
            << " " << courses[i].start_time << "-" << courses[i].end_time
            << " | " << courses[i].room_number << " " << courses[i].building << std::endl;
        std::cout << "Enter course number to add: ";
        int cidx;
        std::cin >> cidx;
        if (cidx < 1 || cidx >(int)courses.size())
        {
            std::cout << "Invalid.\n";
            return;
        }
        auto& sc = courses[cidx - 1];
        if (db.isAlreadyEnrolled(id, sc.schedule_id))
        {
            std::cout << "Already enrolled in this course.\n";
            return;
        }
        if (db.hasClash(id, sc.timeslot_id))
        {
            std::cout << "Course timeslot clashes with your existing courses.\n";
            return;
        }
        if (db.addEnrollment(id, sc.schedule_id))
            std::cout << "Enrolled successfully.\n";
        else
            std::cout << "Course full or error occurred.\n";
    }
    void dropCourse()
    {
        auto enrolled = db.getEnrolledCourses(id);
        if (enrolled.empty())
        {
            std::cout << "No enrolled courses.\n";
            return;
        }
        for (size_t i = 0; i < enrolled.size(); ++i)
            std::cout << i + 1 << ". " << enrolled[i].course_code << " - " << enrolled[i].course_name << " | "
            << enrolled[i].faculty_name << " | " << enrolled[i].day << " " << enrolled[i].start_time << "-" << enrolled[i].end_time << std::endl;
        std::cout << "Enter course number to drop: ";
        int cidx;
        std::cin >> cidx;
        if (cidx < 1 || cidx >(int)enrolled.size())
        {
            std::cout << "Invalid.\n";
            return;
        }
        int schedule_id = enrolled[cidx - 1].schedule_id;
        if (db.dropEnrollment(id, schedule_id))
            std::cout << "Dropped successfully.\n";
        else
            std::cout << "Error or not enrolled.\n";
    }
    void viewTimetable()
    {
        auto tt = db.getStudentTimetable(id);
        if (tt.empty())
        {
            std::cout << "No enrolled courses.\n";
            return;
        }
        std::cout << CYAN << std::left << std::setw(10) << "Course" << std::setw(50) << "Name" << std::setw(15) << "Day"
            << std::setw(12) << "Start" << std::setw(12) << "End" << std::setw(10) << "Room"
            << std::setw(10) << "Bldg" << std::setw(20) << "Teacher" << RESET << std::endl;
        for (size_t i = 0; i < tt.size(); ++i)
        {
            const auto& t = tt[i];
            std::cout << std::setw(10) << t.course_code << std::setw(50) << t.course_name << std::setw(15) << t.day
                << std::setw(12) << t.start_time << std::setw(12) << t.end_time << std::setw(10) << t.room_number
                << std::setw(10) << t.building << std::setw(20) << t.faculty_name << std::endl;
        }
    }
    void viewTeachers()
    {
        auto tt = db.getStudentTimetable(id);
        std::cout << "Your Teachers:\n";
        for (size_t i = 0; i < tt.size(); ++i)
        {
            bool alreadyShown = false;
            for (size_t j = 0; j < i; ++j)
            {
                if (tt[j].faculty_name == tt[i].faculty_name)
                {
                    alreadyShown = true;
                    break;
                }
            }
            if (!alreadyShown)
            {
                std::cout << "- " << tt[i].faculty_name << std::endl;
            }
        }
    }
    void viewClassroomDetails()
    {
        auto tt = db.getStudentTimetable(id);
        std::cout << "Your Classrooms:\n";
        for (size_t i = 0; i < tt.size(); ++i)
        {
            bool alreadyShown = false;
            for (size_t j = 0; j < i; ++j)
            {
                if (tt[j].room_number == tt[i].room_number && tt[j].building == tt[i].building)
                {
                    alreadyShown = true;
                    break;
                }
            }
            if (!alreadyShown)
            {
                std::cout << "- Room " << tt[i].room_number << " in " << tt[i].building << std::endl;
            }
        }
    }
    void exportTimetable()
    {
        auto tt = db.getStudentTimetable(id);
        std::ofstream out(id + "_timetable.csv");
        out << "Course,Name,Day,Start,End,Room,Bldg,Teacher\n";
        for (size_t i = 0; i < tt.size(); ++i)
        {
            const auto& t = tt[i];
            out << t.course_code << "," << t.course_name << "," << t.day << "," << t.start_time << ","
                << t.end_time << "," << t.room_number << "," << t.building << "," << t.faculty_name << "\n";
        }
        out.close();
        std::cout << "Timetable exported to " << id << "_timetable.csv\n";
    }
    void changePassword()
    {
        std::string oldPwd, newPwd;
        std::cout << "Enter current password: ";
        std::cin >> oldPwd;
        if (!db.validateStudentPassword(id, oldPwd))
        {
            std::cout << "Current password incorrect.\n";
            return;
        }
        std::cout << "Enter new password: ";
        std::cin >> newPwd;
        if (db.changeStudentPassword(id, newPwd))
            std::cout << "Password changed successfully.\n";
        else
            std::cout << "Failed to change password.\n";
    }
    void viewMarks()
    {
    auto courses = db.getStudentCourses(id);
    if (courses.empty())
    {
        std::cout << "You are not enrolled in any courses.\n";
        return;
    }

    std::cout << "\n" << CYAN << "Your Courses:" << RESET << "\n";
    for (size_t i = 0; i < courses.size(); ++i)
    {
        std::cout << i + 1 << ". " << courses[i] << "\n";
    }

    std::cout << "\nSelect course to view marks (0 to view all): ";
    int choice;
    std::cin >> choice;

    if (choice < 0 || choice > (int)courses.size())
    {
        std::cout << "Invalid selection.\n";
        return;
    }

    std::string course_code;
    if (choice > 0)
    {
        course_code = courses[choice - 1].substr(0, courses[choice - 1].find(" - "));
    }

    auto marks = db.getStudentMarks(id, course_code);

    if (marks.empty())
    {
        std::cout << "No marks available for the selected course(s).\n";
        return;
    }

    std::cout << "\n" << CYAN << "Your Marks:" << RESET << "\n";
    std::cout << CYAN << std::left << std::setw(25) << "Assignment"
              << std::setw(15) << "Marks"
              << std::setw(12) << "Percentage\n";
    std::cout << std::string(50, '-') << RESET << "\n";

    for (const auto& mark : marks)
    {
        double percentage = (static_cast<double>(mark.obtained_marks) / mark.total_marks) * 100;

        // Apply color based on percentage BEFORE printing
        std::string color = RESET;
        if (percentage >= 80) {
            color = GREEN;
        } else if (percentage >= 50) {
            color = YELLOW;
        } else if (percentage < 50) {
            color = RED;
        }

        // Create the marks string
        std::string marks_str = std::to_string(mark.obtained_marks) + "/" + std::to_string(mark.total_marks);

        // Print the entire line with color
        std::ostringstream percStream;
        percStream << std::fixed << std::setprecision(2) << static_cast<float>(percentage);

        std::cout << color << std::left << std::setw(25) << mark.assignment_name
                  << std::setw(15) << marks_str
                  << std::setw(12) << (percStream.str() + "%") << RESET << "\n";

    }
    std::cout << "\n";
    }
};

class Faculty : public Person
{
    Database& db;

public:
    Faculty(Database& db, const std::string& id, const std::string& name, const std::string& email)
        : Person(id, name, email), db(db)
    {}
    void menu() override
    {
        int choice;
        do
        {
            std::cout << CYAN << "\n--- Faculty Menu ---\n" << RESET;
            std::cout << "1. View Enrolled Students\n";
            std::cout << "2. View My Timetable\n";
            std::cout << "3. Export Timetable\n";
            std::cout << "4. Manage Marks\n";
            std::cout << "5. View Total Enrolled Students\n";
            std::cout << "6. Change Password\n";
            std::cout << "0. Logout\n";
            std::cout << "Choice: ";
            std::cin >> choice;
            switch (choice)
            {
            case 1:
                viewEnrolledStudents();
                break;
            case 2:
                viewTimetable();
                break;
            case 3:
                exportTimetable();
                break;
            case 4:
                manageMarks();
                break;
            case 5:
                viewTotalEnrolledStudents();
                break;
            case 6:
                changePassword();
                break;
            case 0:
                std::cout << "Logging out...\n";
                break;
            default:
                std::cout << "Invalid choice.\n";
            }
        } while (choice != 0);
    }
    std::string getRole() const override { return "Faculty"; }

    void viewEnrolledStudents()
    {
        auto courses = db.getFacultyCourses(std::stoi(id));
        if (courses.empty())
        {
            std::cout << "You are not assigned to any courses.\n";
            return;
        }

        std::cout << "Your courses:\n";
        for (size_t i = 0; i < courses.size(); ++i)
        {
            std::cout << i + 1 << ". " << courses[i] << std::endl;
        }

        std::cout << "Select course to view enrolled students: ";
        int choice;
        std::cin >> choice;

        if (choice < 1 || choice > (int)courses.size())
        {
            std::cout << "Invalid choice.\n";
            return;
        }

        std::string course_code = courses[choice - 1].substr(0, courses[choice - 1].find(" - "));
        auto students = db.getEnrolledStudentsInCourse(course_code);

        if (students.empty())
        {
            std::cout << "No students enrolled in this course.\n";
            return;
        }

        // Sort students by first name
        std::sort(students.begin(), students.end(), [](const Database::StudentInfo& a, const Database::StudentInfo& b) {
            return a.first_name < b.first_name; // Sort by first name
        });

        std::cout << CYAN << std::left << std::setw(12) << "Student ID" << std::setw(20) << "First Name"
                  << std::setw(20) << "Last Name" << std::setw(25) << "Email" << std::setw(8) << "Sem"
                  << std::setw(15) << "Degree" << RESET << std::endl;

        for (const auto& student : students)
        {
            std::cout << std::setw(12) << student.student_id << std::setw(20) << student.first_name
                      << std::setw(20) << student.last_name << std::setw(25) << student.email
                      << std::setw(8) << student.semester << std::setw(15) << student.degree << std::endl;
        }
    }

    void viewTimetable()
    {
        auto tt = db.getFacultyTimetable(std::stoi(id));
        if (tt.empty())
        {
            std::cout << "No classes scheduled.\n";
            return;
        }
        std::cout << CYAN << std::left << std::setw(10) << "Course" << std::setw(50) << "Name" << std::setw(15) << "Day"
            << std::setw(12) << "Start" << std::setw(12) << "End" << std::setw(10) << "Room"
            << std::setw(10) << "Bldg" << RESET << std::endl;
        for (const auto& t : tt)
        {
            std::cout << std::setw(10) << t.course_code << std::setw(50) << t.course_name << std::setw(15) << t.day
                << std::setw(12) << t.start_time << std::setw(12) << t.end_time << std::setw(10) << t.room_number
                << std::setw(10) << t.building << std::endl;
        }
    }

    void exportTimetable()
    {
        auto tt = db.getFacultyTimetable(std::stoi(id));
        if (tt.empty())
        {
            std::cout << "No classes to export.\n";
            return;
        }
        std::ofstream out("faculty_" + id + "_timetable.csv");
        out << "Course,Name,Day,Start,End,Room,Bldg\n";
        for (const auto& t : tt)
        {
            out << t.course_code << "," << t.course_name << "," << t.day << "," << t.start_time << ","
                << t.end_time << "," << t.room_number << "," << t.building << "\n";
        }
        out.close();
        std::cout << "Timetable exported to faculty_" << id << "_timetable.csv\n";
    }

    void changePassword()
    {
        std::string oldPwd, newPwd;
        std::cout << "Enter current password: ";
        std::cin >> oldPwd;
        if (!db.validateFacultyPassword(email, oldPwd))
        {
            std::cout << "Current password incorrect.\n";
            return;
        }
        std::cout << "Enter new password: ";
        std::cin >> newPwd;
        if (db.changeFacultyPassword(email, newPwd))
            std::cout << "Password changed successfully.\n";
        else
            std::cout << "Failed to change password.\n";
    }

    void manageMarks() {
        while (true) {
            std::cout << CYAN << "\n--- Marks Management ---\n" << RESET;
            std::cout << "1. Add Marks for Students\n";
            std::cout << "2. Edit Existing Marks\n";
            std::cout << "0. Back to Main Menu\n";
            std::cout << "Choice: ";

            int choice;
            std::cin >> choice;

            if (choice == 0) {
                break;
            }

            switch (choice) {
                case 1:
                    addMarks();
                    break;
                case 2:
                    editMarks();
                    break;
                default:
                    std::cout << "Invalid choice. Please try again.\n";
            }
        }
    }

    void addMarks() {
        auto courses = db.getFacultyCourses(std::stoi(id));
        if (courses.empty()) {
            std::cout << "You are not assigned to any courses.\n";
            return;
        }

        std::cout << "\nYour courses:\n";
        for (size_t i = 0; i < courses.size(); ++i) {
            std::cout << i + 1 << ". " << courses[i] << std::endl;
        }

        std::cout << "Select course to add marks: ";
        int course_choice;
        std::cin >> course_choice;

        if (course_choice < 1 || course_choice > (int)courses.size()) {
            std::cout << "Invalid choice.\n";
            return;
        }

        std::string course_code = courses[course_choice - 1].substr(0, courses[course_choice - 1].find(" - "));
        std::string course_name = courses[course_choice - 1].substr(courses[course_choice - 1].find(" - ") + 3);

        // Select assignment
        std::string assignment_name;
        std::cout << "Enter assignment name (e.g., Assignment1, Midterm, Final): ";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::getline(std::cin, assignment_name);

        int total_marks;
        std::cout << "Enter total marks for this assignment: ";
        std::cin >> total_marks;

        auto students = db.getEnrolledStudentsInCourse(course_code);
        if (students.empty()) {
            std::cout << "No students enrolled in this course.\n";
            return;
        }

        // Sort students by first name
        std::sort(students.begin(), students.end(), [](const Database::StudentInfo& a, const Database::StudentInfo& b) {
            return a.first_name < b.first_name;
        });

        // Filter out students who already have marks for this assignment
        std::vector<Database::StudentInfo> students_without_marks;
        auto existing_marks = db.getStudentMarksForAssignment(course_code, assignment_name);

        for (const auto& student : students) {
            bool has_marks = false;
            for (const auto& mark : existing_marks) {
                if (mark.first == student.student_id) {
                    has_marks = true;
                    break;
                }
            }
            if (!has_marks) {
                students_without_marks.push_back(student);
            }
        }

        if (students_without_marks.empty()) {
            std::cout << "All students already have marks for this assignment.\n";
            return;
        }

        while (!students_without_marks.empty()) {
            std::cout << "\n" << CYAN << "Course: " << course_name << RESET << "\n";
            std::cout << CYAN << "Assignment: " << assignment_name << " (Total Marks: " << total_marks << ")" << RESET << "\n";
            std::cout << "\nStudents remaining to mark:\n";

            for (size_t i = 0; i < students_without_marks.size(); ++i) {
                std::cout << i + 1 << ". " << students_without_marks[i].student_id << " - "
                          << students_without_marks[i].first_name << " " << students_without_marks[i].last_name << "\n";
            }

            std::cout << "\nSelect student to add marks (0 to finish): ";
            int student_choice;
            std::cin >> student_choice;

            if (student_choice == 0) {
                break;
            }

            if (student_choice < 1 || student_choice > (int)students_without_marks.size()) {
                std::cout << "Invalid choice.\n";
                continue;
            }

            const auto& student = students_without_marks[student_choice - 1];
            int obtained_marks;
            std::cout << "Enter obtained marks for " << student.first_name << " " << student.last_name << ": ";
            std::cin >> obtained_marks;

            db.addMarks(course_code, student.student_id, assignment_name, total_marks, obtained_marks);
            std::cout << "Marks added successfully for " << student.first_name << " " << student.last_name << "\n";

            // Remove the student from the list
            students_without_marks.erase(students_without_marks.begin() + student_choice - 1);
        }
    }

    void editMarks() {
        auto courses = db.getFacultyCourses(std::stoi(id));
        if (courses.empty()) {
            std::cout << "You are not assigned to any courses.\n";
            return;
        }

        std::cout << "\nYour courses:\n";
        for (size_t i = 0; i < courses.size(); ++i) {
            std::cout << i + 1 << ". " << courses[i] << std::endl;
        }

        std::cout << "Select course to edit marks: ";
        int course_choice;
        std::cin >> course_choice;

        if (course_choice < 1 || course_choice > (int)courses.size()) {
            std::cout << "Invalid choice.\n";
            return;
        }

        std::string course_code = courses[course_choice - 1].substr(0, courses[course_choice - 1].find(" - "));
        std::string course_name = courses[course_choice - 1].substr(courses[course_choice - 1].find(" - ") + 3);

        auto assignments = db.getAssignmentsForCourse(course_code);
        if (assignments.empty()) {
            std::cout << "No assignments found for this course.\n";
            return;
        }

        std::cout << "\nAssignments for " << course_name << ":\n";
        for (size_t i = 0; i < assignments.size(); ++i) {
            std::cout << i + 1 << ". " << assignments[i] << std::endl;
        }

        std::cout << "Select assignment to edit: ";
        int assignment_choice;
        std::cin >> assignment_choice;

        if (assignment_choice < 1 || assignment_choice > (int)assignments.size()) {
            std::cout << "Invalid choice.\n";
            return;
        }

        std::string assignment_name = assignments[assignment_choice - 1];
        auto marks = db.getStudentMarksForAssignment(course_code, assignment_name);

        if (marks.empty()) {
            std::cout << "No marks found for this assignment.\n";
            return;
        }

        // Get all students to display names
        auto all_students = db.getEnrolledStudentsInCourse(course_code);
        std::map<std::string, std::pair<std::string, std::string>> student_names; // student_id -> (first_name, last_name)
        for (const auto& student : all_students) {
            student_names[student.student_id] = {student.first_name, student.last_name};
        }

        while (true) {
            std::cout << "\n" << CYAN << "Course: " << course_name << RESET << "\n";
            std::cout << CYAN << "Assignment: " << assignment_name << RESET << "\n\n";

            std::cout << CYAN << std::left << std::setw(5) << "No." << std::setw(15) << "Student ID"
                      << std::setw(25) << "Name" << std::setw(15) << "Marks" << RESET << "\n";
            std::cout << std::string(60, '-') << "\n";

            for (size_t i = 0; i < marks.size(); ++i) {
                const auto& mark = marks[i];
                auto student_info = student_names[mark.first];
                std::cout << std::setw(5) << i + 1
                          << std::setw(15) << mark.first
                          << std::setw(25) << (student_info.first + " " + student_info.second)
                          << std::setw(15) << (std::to_string(mark.second.second) + "/" + std::to_string(mark.second.first))
                          << "\n";
            }

            std::cout << "\nSelect student to edit marks (0 to finish): ";
            int student_choice;
            std::cin >> student_choice;

            if (student_choice == 0) {
                break;
            }

            if (student_choice < 1 || student_choice > (int)marks.size()) {
                std::cout << "Invalid choice.\n";
                continue;
            }

            const auto& selected_mark = marks[student_choice - 1];
            auto student_info = student_names[selected_mark.first];
            int new_marks;

            std::cout << "Current marks for " << student_info.first << " " << student_info.second
                      << ": " << selected_mark.second.second << "/" << selected_mark.second.first << "\n";
            std::cout << "Enter new obtained marks: ";
            std::cin >> new_marks;

            if (new_marks < 0 || new_marks > selected_mark.second.first) {
                std::cout << "Marks must be between 0 and " << selected_mark.second.first << "\n";
                continue;
            }

            db.updateMarks(course_code, selected_mark.first, assignment_name, new_marks);
            std::cout << "Marks updated successfully.\n";

            // Refresh the marks list
            marks = db.getStudentMarksForAssignment(course_code, assignment_name);
        }
    }

    void viewTotalEnrolledStudents()
    {
        auto courses = db.getFacultyCourses(std::stoi(id));
        if (courses.empty())
        {
            std::cout << "You are not assigned to any courses.\n";
            return;
        }

        std::cout << CYAN << "Course Enrollment Summary:\n" << RESET;
        std::cout << std::left << std::setw(15) << "Course Code" << std::setw(50) << "Course Name"
                  << std::setw(15) << "Total Students" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        for (const auto& course : courses)
        {
            std::string course_code = course.substr(0, course.find(" - "));
            std::string course_name = course.substr(course.find(" - ") + 3);
            int total_students = db.getTotalEnrolledStudents(course_code);

            std::cout << std::setw(15) << course_code << std::setw(50) << course_name
                      << std::setw(15) << total_students << std::endl;
        }
    }
};

class Admin : public Person
{
    Database& db;

public:
    Admin(Database& db, const std::string& id, const std::string& name, const std::string& email)
        : Person(id, name, email), db(db)
    {}
    void menu() override
    {
        int choice;
        do
        {
            std::cout << CYAN << "\n--- Admin Menu ---\n" << RESET;
            std::cout << "1. Add Student\n";
            std::cout << "2. Remove Student\n";
            std::cout << "3. Add Faculty\n";
            std::cout << "4. Remove Faculty\n";
            std::cout << "5. Add Course\n";
            std::cout << "6. Remove Course\n";
            std::cout << "7. Add Classroom\n";
            std::cout << "8. Remove Classroom\n";
            std::cout << "9. Add Timeslot\n";
            std::cout << "10. Remove Timeslot\n";
            std::cout << "11. Assign Course/Teacher/Timeslot/Classroom\n";
            std::cout << "12. Remove Course Assignment\n";
            std::cout << "13. Reset Student Password\n";
            std::cout << "14. Reset Faculty Password\n";
            std::cout << "0. Logout\n";
            std::cout << "Choice: ";
            std::cin >> choice;
            switch (choice)
            {
            case 1:
                addStudent();
                break;
            case 2:
                removeStudent();
                break;
            case 3:
                addFaculty();
                break;
            case 4:
                removeFaculty();
                break;
            case 5:
                addCourse();
                break;
            case 6:
                removeCourse();
                break;
            case 7:
                addClassroom();
                break;
            case 8:
                removeClassroom();
                break;
            case 9:
                addTimeslot();
                break;
            case 10:
                removeTimeslot();
                break;
            case 11:
                assignCourseSchedule();
                break;
            case 12:
                removeCourseAssignment();
                break;
            case 13:
                resetStudentPassword();
                break;
            case 14:
                resetFacultyPassword();
                break;
            case 0:
                std::cout << "Logging out...\n";
                break;
            default:
                std::cout << "Invalid choice.\n";
            }
        } while (choice != 0);
    }
    std::string getRole() const override { return "Admin"; }
    void addStudent()
    {
        std::string id, fname, lname, email, degree;
        int semester;
        std::cout << "Student ID: ";
        std::cin >> id;
        std::cin.ignore();
        std::cout << "First name: ";
        std::getline(std::cin, fname);
        std::cout << "Last name: ";
        std::getline(std::cin, lname);
        std::cout << "Email: ";
        std::cin >> email;
        std::cin.ignore();
        std::cout << "Degree: ";
        std::getline(std::cin, degree);
        std::cout << "Semester: ";
        std::cin >> semester;
        db.addStudent(id, fname, lname, email, degree, semester); // Will set password to "bnu"
        std::cout << "Student added (default password 'bnu').\n";
    }
    void removeStudent()
    {
        std::string id;
        std::cout << "Student ID to remove: ";
        std::cin >> id;
        db.removeStudent(id);
        std::cout << "Student removed.\n";
    }
    void addFaculty()
    {
        int faculty_id;
        std::string fname, lname, email, degree, qualification, expertise_sub, designation;
        std::cout << "Faculty ID: ";
        std::cin >> faculty_id;
        std::cin.ignore();
        std::cout << "First name: ";
        std::getline(std::cin, fname);
        std::cout << "Last name: ";
        std::getline(std::cin, lname);
        std::cout << "Email: ";
        std::cin >> email;
        std::cin.ignore();
        std::cout << "Degree: ";
        std::getline(std::cin, degree);
        std::cout << "Qualification: ";
        std::cin >> qualification;
        std::cin.ignore();
        std::cout << "Expertise subject: ";
        std::getline(std::cin, expertise_sub);
        std::cout << "Designation: ";
        std::getline(std::cin, designation);
        db.addFaculty(faculty_id, fname, lname, email, degree, qualification, expertise_sub, designation);
        std::cout << "Faculty added (default password 'faculty_scit').\n";
    }
    void removeFaculty()
    {
        int id;
        std::cout << "Faculty ID to remove: ";
        std::cin >> id;
        db.removeFaculty(id);
        std::cout << "Faculty removed.\n";
    }
    void addCourse()
    {
        std::string code, name, dept, prereq;
        int sem, max, credits;
        std::cout << "Course code: ";
        std::cin >> code;
        std::cin.ignore();
        std::cout << "Course name: ";
        std::getline(std::cin, name);
        std::cout << "Credits: ";
        std::cin >> credits;
        std::cout << "Semester: ";
        std::cin >> sem;
        std::cin.ignore();
        std::cout << "Department: ";
        std::getline(std::cin, dept);
        std::cout << "Max students: ";
        std::cin >> max;
        std::cin.ignore();
        std::cout << "Prerequisites: ";
        std::getline(std::cin, prereq);
        db.addCourse(code, name, credits, sem, dept, max, prereq);
        std::cout << "Course added.\n";
    }
    void removeCourse()
    {
        std::string code;
        std::cout << "Course code to remove: ";
        std::cin >> code;
        db.removeCourse(code);
        std::cout << "Course removed.\n";
    }
    void addClassroom()
    {
        std::string id, number, building, room_type;
        int capacity;
        std::cout << "Room ID: ";
        std::cin >> id;
        std::cout << "Room number: ";
        std::cin >> number;
        std::cout << "Building: ";
        std::cin >> building;
        std::cout << "Capacity: ";
        std::cin >> capacity;
        std::cout << "Room type: ";
        std::cin >> room_type;
        db.addClassroom(id, building, number, capacity, room_type);
        std::cout << "Classroom added.\n";
    }
    void removeClassroom()
    {
        std::string id;
        std::cout << "Room ID to remove: ";
        std::cin >> id;
        db.removeClassroom(id);
        std::cout << "Classroom removed.\n";
    }
    void addTimeslot()
    {
        std::string day, start, end;
        std::cout << "Day of week: ";
        std::cin >> day;
        std::cout << "Start time (HH:MM:SS): ";
        std::cin >> start;
        std::cout << "End time (HH:MM:SS): ";
        std::cin >> end;
        db.addTimeslot(day, start, end);
        std::cout << "Timeslot added.\n";
    }
    void removeTimeslot()
    {
        int id;
        std::cout << "Timeslot ID to remove: ";
        std::cin >> id;
        db.removeTimeslot(id);
        std::cout << "Timeslot removed.\n";
    }
    void assignCourseSchedule()
    {
        auto courses = db.getUnscheduledCourses();
        if (courses.empty())
        {
            std::cout << "All courses are already assigned. Remove an assignment to reassign.\n";
            return;
        }
        auto timeslots = db.getAllTimeslots();
        int c, f, t, r;
        std::cout << "Courses:\n";
        for (size_t i = 0; i < courses.size(); ++i)
            std::cout << i + 1 << ". " << courses[i].second << std::endl;
        std::cout << "Select course: ";
        std::cin >> c;
        std::cout << "Timeslots:\n";
        for (size_t i = 0; i < timeslots.size(); ++i)
            std::cout << timeslots[i].first << " - " << timeslots[i].second << std::endl;
        std::cout << "Select timeslot: ";
        std::cin >> t;
        if (c < 1 || c >(int)courses.size() || t < 1 || t >(int)timeslots.size())
        {
            std::cout << "Invalid selection.\n";
            return;
        }
        auto availableFaculty = db.getAvailableFaculty(timeslots[t - 1].first);
        if (availableFaculty.empty())
        {
            std::cout << "No available faculty for this timeslot.\n";
            return;
        }
        std::cout << "Faculty:\n";
        for (size_t i = 0; i < availableFaculty.size(); ++i)
            std::cout << i + 1 << ". " << availableFaculty[i].second << std::endl;
        std::cout << "Select faculty: ";
        std::cin >> f;
        if (f < 1 || f >(int)availableFaculty.size())
        {
            std::cout << "Invalid selection.\n";
            return;
        }
        auto rooms = db.getAvailableRooms(timeslots[t - 1].first);
        if (rooms.empty())
        {
            std::cout << "No available rooms for this timeslot.\n";
            return;
        }
        std::cout << "Rooms:\n";
        for (size_t i = 0; i < rooms.size(); ++i)
            std::cout << i + 1 << ". " << rooms[i].second << std::endl;
        std::cout << "Select room: ";
        std::cin >> r;
        if (r < 1 || r >(int)rooms.size())
        {
            std::cout << "Invalid selection.\n";
            return;
        }
        db.addCourseSchedule(
            courses[c - 1].first,
            availableFaculty[f - 1].first,
            timeslots[t - 1].first,
            rooms[r - 1].first);
        std::cout << "Assignment completed.\n";
    }
    void removeCourseAssignment()
    {
        auto assignments = db.getAllCourseSchedules();
        if (assignments.empty())
        {
            std::cout << "No assigned courses.\n";
            return;
        }
        for (size_t i = 0; i < assignments.size(); ++i)
            std::cout << i + 1 << ". " << assignments[i].course_code << " - " << assignments[i].course_name
            << " | " << assignments[i].faculty_name << " | " << assignments[i].room << " | " << assignments[i].timeslot << std::endl;
        std::cout << "Select assignment to remove: ";
        int idx;
        std::cin >> idx;
        if (idx < 1 || idx >(int)assignments.size())
        {
            std::cout << "Invalid selection.\n";
            return;
        }
        db.removeCourseSchedule(assignments[idx - 1].schedule_id);
        std::cout << "Assignment removed.\n";
    }
    void resetStudentPassword()
    {
        std::string studentId;
        std::cout << "Enter Student ID to reset password: ";
        std::cin >> studentId;
        if (db.resetStudentPassword(studentId))
            std::cout << "Password reset to 'bnu'.\n";
        else
            std::cout << "Student not found or failed to reset password.\n";
    }
    void resetFacultyPassword()
    {
        std::string email;
        std::cout << "Enter Faculty Email to reset password: ";
        std::cin >> email;
        if (db.facultyExists(email))
        {
            if (db.resetFacultyPassword(email))
                std::cout << "Password reset to 'faculty_scit'.\n";
            else
                std::cout << "Failed to reset password.\n";
        }
        else
        {
            std::cout << "Faculty not found.\n";
        }
    }
};

int main()
{
    std::string host = "127.0.0.1";
    std::string user = "root";
    std::string pass = "Sufian312";
    std::string dbname = "project_db";
    try
    {
        Database db(host, user, pass, dbname);
        int choice;
        do
        {
            std::cout << CYAN << "\n--- SCIT Management System ---" << RESET << std::endl;
            std::cout << "1. Student Login\n";
            std::cout << "2. Admin Login\n";
            std::cout << "3. Faculty Login\n";
            std::cout << "0. Exit\n";
            std::cout << "Choice: ";
            std::cin >> choice;
            if (choice == 1)
            {
                std::string studentId, password;
                std::cout << "Enter Student ID: ";
                std::cin >> studentId;
                std::cout << "Enter Password: ";
                std::cin >> password;
                if (db.studentExists(studentId) && db.validateStudentPassword(studentId, password))
                {
                    // Get student name from database (you'll need to implement this in Database class)
                    std::string studentName = "Student"; // Replace with actual name from DB
                    Student stu(db, studentId, studentName, studentId + "@bnu.edu.pk");
                    stu.menu();
                }
                else
                {
                    std::cout << "Invalid Student ID or Password.\n";
                }
            }
            else if (choice == 2)
            {
                std::string password;
                std::cout << "Enter Admin Password: ";
                std::cin >> password;
                if (db.isAdminPasswordCorrect(password))
                {
                    Admin admin(db, "admin", "Admin", "admin@email.com");
                    admin.menu();
                }
                else
                {
                    std::cout << "Invalid password.\n";
                }
            }
            else if (choice == 3)
            {
                std::string email, password;
                std::cout << "Enter Faculty Email (without @bnu.edu.pk): ";
                std::cin >> email;
                email += "@bnu.edu.pk"; // Append the domain automatically
                std::cout << "Enter Password: ";
                std::cin >> password;
                if (db.facultyExists(email) && db.validateFacultyPassword(email, password))
                {
                    int facultyId = std::stoi(db.getFacultyId(email));
                    std::string facultyName = db.getFacultyName(email);
                    Faculty faculty(db, std::to_string(facultyId), facultyName, email);
                    faculty.menu();
                }
                else
                {
                    std::cout << "Invalid Faculty Email or Password.\n";
                }
            }
            else if (choice == 0)
            {
                std::cout << "Exiting...\n";
            }
            else
            {
                std::cout << "Invalid choice.\n";
            }
        } while (choice != 0);
    }
    catch (const mysqlx::Error& ex)
    {
        std::cerr << "Database error: " << ex.what() << std::endl;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << std::endl;
    }
    return 0;
}