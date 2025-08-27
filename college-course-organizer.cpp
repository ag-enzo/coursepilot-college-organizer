// CoursePilot: Qt 6 app for organizing college courses and assignments.

#include <QtWidgets>
#include <QtSql>
#include <vector>
#include <queue>
#include <string>
#include <string_view>
#include <chrono>

// Domain types and enum mapping
enum class AssignType { HW, Quiz, Midterm, Final, Project, Essay, Other };

static QString toString(AssignType t) {
    switch (t) {
    case AssignType::HW: return "HW";
    case AssignType::Quiz: return "Quiz";
    case AssignType::Midterm: return "Midterm";
    case AssignType::Final: return "Final";
    case AssignType::Project: return "Project";
    case AssignType::Essay: return "Essay";
    default: return "Other";
    }
}

static AssignType parseAssignType(const QString& s) {
    if (s == "HW") return AssignType::HW;
    if (s == "Quiz") return AssignType::Quiz;
    if (s == "Midterm") return AssignType::Midterm;
    if (s == "Final") return AssignType::Final;
    if (s == "Project") return AssignType::Project;
    if (s == "Essay") return AssignType::Essay;
    return AssignType::Other;
}

struct Semester { int id{}; QString term; int year{}; };

struct Course {
    int id{}, userId{}, semesterId{};
    QString code, name, colorHex;
};

struct Assignment {
    int id{}, courseId{};
    AssignType type{AssignType::Other};
    QString title;
    QDateTime dueAtUtc;
    std::optional<QString> topics, notes;
};

// Min-heap comparator for "Upcoming" deadlines (soonest first)
struct DueSooner {
    bool operator()(const Assignment& a, const Assignment& b) const {
        return a.dueAtUtc > b.dueAtUtc;
    }
};

// SQLite DB setup and migrations
static QString appDataPath() {
    auto p = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(p);
    return p;
}

static bool ensureDbOpen(QSqlDatabase& db) {
    if (db.isOpen()) return true;
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(appDataPath() + "/coursepilot.db");
    return db.open();
}

static bool runMigrations(QSqlDatabase& db) {
    QSqlQuery q(db);
    if (!q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS users(
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          username TEXT UNIQUE NOT NULL,
          password_hash BLOB NOT NULL,
          created_at INTEGER NOT NULL
        );
    )SQL")) return false;

    if (!q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS semesters(
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          term TEXT NOT NULL CHECK(term IN ('Fall','Spring')),
          year INTEGER NOT NULL
        );
    )SQL")) return false;

    if (!q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS courses(
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          user_id INTEGER NOT NULL,
          semester_id INTEGER NOT NULL,
          code TEXT NOT NULL,
          name TEXT NOT NULL,
          color_hex TEXT NOT NULL,
          FOREIGN KEY(user_id) REFERENCES users(id),
          FOREIGN KEY(semester_id) REFERENCES semesters(id)
        );
    )SQL")) return false;

    if (!q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS assignments(
          id INTEGER PRIMARY KEY AUTOINCREMENT,
          course_id INTEGER NOT NULL,
          type TEXT NOT NULL,
          title TEXT NOT NULL,
          due_at_utc INTEGER NOT NULL,
          topics TEXT NULL,
          notes TEXT NULL,
          FOREIGN KEY(course_id) REFERENCES courses(id)
        );
    )SQL")) return false;

    return true;
}

static QByteArray hashPassword(const QString& pw) {
    return QCryptographicHash::hash(pw.toUtf8(), QCryptographicHash::Sha256);
}

// AuthDialog: Register/Login
class AuthDialog : public QDialog {
    Q_OBJECT
public:
    explicit AuthDialog(QWidget* parent=nullptr) : QDialog(parent) {
        setWindowTitle("CoursePilot – Sign in");
        auto form = new QFormLayout;
        user_ = new QLineEdit;
        pass_ = new QLineEdit; pass_->setEchoMode(QLineEdit::Password);
        form->addRow("Username", user_);
        form->addRow("Password", pass_);

        auto btnLogin = new QPushButton("Login");
        auto btnRegister = new QPushButton("Register");
        auto row = new QHBoxLayout; row->addWidget(btnLogin); row->addWidget(btnRegister);

        auto v = new QVBoxLayout; v->addLayout(form); v->addLayout(row); setLayout(v);

        connect(btnLogin, &QPushButton::clicked, this, &AuthDialog::onLogin);
        connect(btnRegister, &QPushButton::clicked, this, &AuthDialog::onRegister);
    }
    int userId() const { return userId_; }

private slots:
    void onLogin() {
        QSqlQuery q;
        q.prepare("SELECT id, password_hash FROM users WHERE username = ?");
        q.addBindValue(user_->text());
        if (!q.exec() || !q.next()) { QMessageBox::warning(this, "Login failed", "User not found."); return; }
        const int id = q.value(0).toInt();
        const auto stored = q.value(1).toByteArray();
        if (stored != hashPassword(pass_->text())) {
            QMessageBox::warning(this, "Login failed", "Incorrect password."); return;
        }
        userId_ = id; accept();
    }
    void onRegister() {
        QSqlDatabase::database().transaction();
        QSqlQuery q;
        q.prepare("INSERT INTO users(username, password_hash, created_at) VALUES(?,?,?)");
        q.addBindValue(user_->text());
        q.addBindValue(hashPassword(pass_->text()));
        q.addBindValue(QDateTime::currentSecsSinceEpoch());
        if (!q.exec()) { QSqlDatabase::database().rollback(); QMessageBox::warning(this, "Register failed", "Username exists?"); return; }
        QSqlDatabase::database().commit();
        QMessageBox::information(this, "Registered", "User created. Please login.");
    }
private:
    QLineEdit *user_{}, *pass_{};
    int userId_ = -1;
};

// SemesterPicker: Select semester
class SemesterPicker : public QDialog {
    Q_OBJECT
public:
    int semesterId{-1};
    SemesterPicker(QWidget* parent=nullptr) : QDialog(parent) {
        setWindowTitle("Select Semester");
        term_ = new QComboBox; term_->addItems({"Fall","Spring"});
        year_ = new QSpinBox; year_->setRange(2022, 2042); year_->setValue(QDate::currentDate().year());

        auto form = new QFormLayout; form->addRow("Term", term_); form->addRow("Year", year_);
        auto ok = new QPushButton("OK");
        auto v = new QVBoxLayout; v->addLayout(form); v->addWidget(ok); setLayout(v);

        connect(ok, &QPushButton::clicked, this, &SemesterPicker::onOk);
    }
private slots:
    void onOk() {
        QSqlQuery q; q.prepare("SELECT id FROM semesters WHERE term=? AND year=?");
        q.addBindValue(term_->currentText()); q.addBindValue(year_->value());
        if (q.exec() && q.next()) { semesterId = q.value(0).toInt(); }
        else {
            QSqlDatabase::database().transaction();
            QSqlQuery ins; ins.prepare("INSERT INTO semesters(term, year) VALUES(?,?)");
            ins.addBindValue(term_->currentText()); ins.addBindValue(year_->value());
            if (ins.exec()) { semesterId = ins.lastInsertId().toInt(); QSqlDatabase::database().commit(); }
            else { QSqlDatabase::database().rollback(); }
        }
        accept();
    }
private:
    QComboBox* term_{};
    QSpinBox* year_{};
};

// CourseDialog: Add/Edit course
class CourseDialog : public QDialog {
    Q_OBJECT
public:
    int courseId{-1};
    // Add optional courseId for editing
    CourseDialog(int userId, int semId, QWidget* parent=nullptr, int editCourseId = -1)
        : QDialog(parent), userId_(userId), semId_(semId), editCourseId_(editCourseId) {
        setWindowTitle(editCourseId_ < 0 ? "Add Course" : "Edit Course");
        code_ = new QLineEdit; code_->setPlaceholderText("Course code (e.g., CS101)");
        name_ = new QLineEdit; name_->setPlaceholderText("Course name");
        color_ = new QLineEdit; color_->setPlaceholderText("Color (optional, hex)");

        auto form = new QFormLayout;
        form->addRow("Code", code_); form->addRow("Name", name_); form->addRow("Color", color_);

        auto btnSave = new QPushButton("Save");
        auto v = new QVBoxLayout; v->addLayout(form); v->addWidget(btnSave); setLayout(v);

        connect(btnSave, &QPushButton::clicked, this, &CourseDialog::onSave);

        // If editing, load course data
        if (editCourseId_ >= 0) {
            QSqlQuery q; q.prepare("SELECT code, name, color_hex FROM courses WHERE id=?");
            q.addBindValue(editCourseId_);
            if (q.exec() && q.next()) {
                code_->setText(q.value(0).toString());
                name_->setText(q.value(1).toString());
                color_->setText(q.value(2).toString());
            }
        }
    }
private slots:
    void onSave() {
        QSqlDatabase::database().transaction();
        if (editCourseId_ < 0) {
            QSqlQuery ins; ins.prepare(R"(INSERT INTO courses(user_id, semester_id, code, name, color_hex) VALUES(?,?,?,?,?))");
            ins.addBindValue(userId_); ins.addBindValue(semId_);
            ins.addBindValue(code_->text()); ins.addBindValue(name_->text());
            ins.addBindValue(color_->text().isEmpty() ? "#4F46E5" : color_->text());
            if (ins.exec()) { courseId = ins.lastInsertId().toInt(); QSqlDatabase::database().commit(); accept(); }
            else { QSqlDatabase::database().rollback(); QMessageBox::warning(this, "Error", "Could not save course."); }
        } else {
            QSqlQuery upd; upd.prepare(R"(UPDATE courses SET code=?, name=?, color_hex=? WHERE id=?)");
            upd.addBindValue(code_->text()); upd.addBindValue(name_->text());
            upd.addBindValue(color_->text().isEmpty() ? "#4F46E5" : color_->text());
            upd.addBindValue(editCourseId_);
            if (upd.exec()) { courseId = editCourseId_; QSqlDatabase::database().commit(); accept(); }
            else { QSqlDatabase::database().rollback(); QMessageBox::warning(this, "Error", "Could not update course."); }
        }
    }
private:
    int userId_, semId_, editCourseId_{-1};
    QLineEdit *code_{}, *name_{}, *color_{};
};

// AssignmentDialog: Add/Edit assignment
class AssignmentDialog : public QDialog {
    Q_OBJECT
public:
    int assignmentId{-1};
    // Add optional assignmentId for editing
    AssignmentDialog(int courseId, QWidget* parent=nullptr, int editAssignmentId = -1)
        : QDialog(parent), courseId_(courseId), editAssignmentId_(editAssignmentId) {
        setWindowTitle(editAssignmentId_ < 0 ? "Add Assignment" : "Edit Assignment");
        type_ = new QComboBox; type_->addItems({"HW","Quiz","Midterm","Final","Project","Essay","Other"});
        title_ = new QLineEdit;
        dueDate_ = new QDateTimeEdit(QDateTime::currentDateTime()); dueDate_->setCalendarPopup(true);
        topics_ = new QLineEdit; topics_->setPlaceholderText("Optional: topics/tags");
        notes_ = new QTextEdit;

        auto form = new QFormLayout;
        form->addRow("Type", type_); form->addRow("Title", title_);
        form->addRow("Due at", dueDate_); form->addRow("Topics", topics_); form->addRow("Notes", notes_);

        auto ok = new QPushButton("Save");
        auto v = new QVBoxLayout; v->addLayout(form); v->addWidget(ok); setLayout(v);
        connect(ok, &QPushButton::clicked, this, &AssignmentDialog::onSave);

        // If editing, load assignment data
        if (editAssignmentId_ >= 0) {
            QSqlQuery q; q.prepare("SELECT type, title, due_at_utc, topics, notes FROM assignments WHERE id=?");
            q.addBindValue(editAssignmentId_);
            if (q.exec() && q.next()) {
                type_->setCurrentText(q.value(0).toString());
                title_->setText(q.value(1).toString());
                dueDate_->setDateTime(QDateTime::fromSecsSinceEpoch(q.value(2).toLongLong()).toLocalTime());
                topics_->setText(q.value(3).toString());
                notes_->setPlainText(q.value(4).toString());
            }
        }
    }
private slots:
    void onSave() {
        QSqlDatabase::database().transaction();
        if (editAssignmentId_ < 0) {
            QSqlQuery ins; ins.prepare(R"(INSERT INTO assignments(course_id, type, title, due_at_utc, topics, notes)
                           VALUES(?,?,?,?,?,?))");
            ins.addBindValue(courseId_);
            ins.addBindValue(type_->currentText());
            ins.addBindValue(title_->text());
            ins.addBindValue(dueDate_->dateTime().toUTC().toSecsSinceEpoch());
            ins.addBindValue(topics_->text().isEmpty() ? QVariant(QString()) : QVariant(topics_->text()));
            ins.addBindValue(notes_->toPlainText().isEmpty() ? QVariant(QString()) : QVariant(notes_->toPlainText()));
            if (ins.exec()) { assignmentId = ins.lastInsertId().toInt(); QSqlDatabase::database().commit(); accept(); }
            else { QSqlDatabase::database().rollback(); QMessageBox::warning(this, "Error", "Could not save assignment."); }
        } else {
            QSqlQuery upd; upd.prepare(R"(UPDATE assignments SET type=?, title=?, due_at_utc=?, topics=?, notes=? WHERE id=?)");
            upd.addBindValue(type_->currentText());
            upd.addBindValue(title_->text());
            upd.addBindValue(dueDate_->dateTime().toUTC().toSecsSinceEpoch());
            upd.addBindValue(topics_->text().isEmpty() ? QVariant(QString()) : QVariant(topics_->text()));
            upd.addBindValue(notes_->toPlainText().isEmpty() ? QVariant(QString()) : QVariant(notes_->toPlainText()));
            upd.addBindValue(editAssignmentId_);
            if (upd.exec()) { assignmentId = editAssignmentId_; QSqlDatabase::database().commit(); accept(); }
            else { QSqlDatabase::database().rollback(); QMessageBox::warning(this, "Error", "Could not update assignment."); }
        }
    }
private:
    int courseId_, editAssignmentId_{-1};
    QComboBox* type_{};
    QLineEdit *title_{}, *topics_{};
    QDateTimeEdit* dueDate_{};
    QTextEdit* notes_{};
};

// MainWindow: Dashboard
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(int userId, QWidget* parent=nullptr) : QMainWindow(parent), userId_(userId) {
        setWindowTitle("CoursePilot (single-file)");
        resize(980, 640);

        // Left column: courses list + add, edit, delete buttons
        courses_ = new QListWidget; courses_->setSelectionMode(QAbstractItemView::SingleSelection);
        auto btnAddCourse = new QPushButton("Add Course");
        auto btnEditCourse = new QPushButton("Edit Course");
        auto btnDeleteCourse = new QPushButton("Delete Course");
        auto left = new QVBoxLayout;
        left->addWidget(new QLabel("Courses"));
        left->addWidget(courses_);
        left->addWidget(btnAddCourse);
        left->addWidget(btnEditCourse);
        left->addWidget(btnDeleteCourse);

        // Center column: assignments table + add, edit, delete buttons
        assigns_ = new QTableWidget(0, 4);
        assigns_->setHorizontalHeaderLabels({"Type","Title","Due (local)","Topics"});
        assigns_->horizontalHeader()->setStretchLastSection(true);
        assigns_->setSelectionBehavior(QAbstractItemView::SelectRows);
        assigns_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        auto btnAddAssign = new QPushButton("Add Assignment");
        auto btnEditAssign = new QPushButton("Edit Assignment");
        auto btnDeleteAssign = new QPushButton("Delete Assignment");
        auto center = new QVBoxLayout;
        center->addWidget(new QLabel("Assignments"));
        center->addWidget(assigns_);
        center->addWidget(btnAddAssign);
        center->addWidget(btnEditAssign);
        center->addWidget(btnDeleteAssign);

        // Right column: Upcoming list (min-heap feed) + refresh
        upcoming_ = new QListWidget;
        auto refreshUpcoming = new QPushButton("Refresh Upcoming");
        auto right = new QVBoxLayout; right->addWidget(new QLabel("Upcoming (soonest first)")); right->addWidget(upcoming_); right->addWidget(refreshUpcoming);

        // Top: term/year pickers
        term_ = new QComboBox; term_->addItems({"Fall","Spring"});
        year_ = new QSpinBox; year_->setRange(2022, 2042); year_->setValue(QDate::currentDate().year());
        auto btnSelectSem = new QPushButton("Use Semester");
        auto top = new QHBoxLayout; top->addWidget(new QLabel("Term:")); top->addWidget(term_);
        top->addWidget(new QLabel("Year:")); top->addWidget(year_); top->addWidget(btnSelectSem); top->addStretch();

        // Grid layout
        auto grid = new QGridLayout;
        grid->addLayout(top, 0, 0, 1, 3);
        grid->addLayout(left, 1, 0);
        grid->addLayout(center, 1, 1);
        grid->addLayout(right, 1, 2);

        auto w = new QWidget; w->setLayout(grid); setCentralWidget(w);

        // Wire actions
        connect(btnSelectSem, &QPushButton::clicked, this, &MainWindow::pickSemester);
        connect(btnAddCourse, &QPushButton::clicked, this, &MainWindow::addCourse);
        connect(btnEditCourse, &QPushButton::clicked, this, &MainWindow::editCourse);
        connect(btnDeleteCourse, &QPushButton::clicked, this, &MainWindow::deleteCourse);
        connect(btnAddAssign, &QPushButton::clicked, this, &MainWindow::addAssignment);
        connect(btnEditAssign, &QPushButton::clicked, this, &MainWindow::editAssignment);
        connect(btnDeleteAssign, &QPushButton::clicked, this, &MainWindow::deleteAssignment);
        connect(courses_, &QListWidget::itemSelectionChanged, this, &MainWindow::loadAssignments);
        connect(refreshUpcoming, &QPushButton::clicked, this, &MainWindow::reloadUpcoming);

        // Prompt for the first semester
        pickSemester();
    }

private slots:
    void pickSemester() {
        SemesterPicker sp(this);
        if (sp.exec() == QDialog::Accepted && sp.semesterId > 0) {
            semesterId_ = sp.semesterId;
            loadSemesterIntoControls();
            loadCourses();
            reloadUpcoming();
        }
    }

    void addCourse() {
        if (semesterId_ < 0) { QMessageBox::information(this,"Select semester","Pick a semester first."); return; }
        CourseDialog cd(userId_, semesterId_, this);
        if (cd.exec() == QDialog::Accepted) loadCourses();
    }

    void editCourse() {
        auto *item = courses_->currentItem();
        if (!item) { QMessageBox::information(this,"Edit course","Select a course."); return; }
        int courseId = item->data(Qt::UserRole).toInt();
        CourseDialog cd(userId_, semesterId_, this, courseId);
        if (cd.exec() == QDialog::Accepted) loadCourses();
    }

    void deleteCourse() {
        auto *item = courses_->currentItem();
        if (!item) { QMessageBox::information(this,"Delete course","Select a course."); return; }
        int courseId = item->data(Qt::UserRole).toInt();
        if (QMessageBox::question(this, "Delete Course", "Are you sure you want to delete this course and all its assignments?") == QMessageBox::Yes) {
            QSqlDatabase::database().transaction();
            QSqlQuery delAssigns; delAssigns.prepare("DELETE FROM assignments WHERE course_id=?");
            delAssigns.addBindValue(courseId); delAssigns.exec();
            QSqlQuery delCourse; delCourse.prepare("DELETE FROM courses WHERE id=?");
            delCourse.addBindValue(courseId); delCourse.exec();
            QSqlDatabase::database().commit();
            loadCourses(); reloadUpcoming();
        }
    }

    void addAssignment() {
        auto *item = courses_->currentItem();
        if (!item) { QMessageBox::information(this,"Add assignment","Select a course."); return; }
        const int courseId = item->data(Qt::UserRole).toInt();
        AssignmentDialog ad(courseId, this);
        if (ad.exec() == QDialog::Accepted) { loadAssignments(); reloadUpcoming(); }
    }

    void editAssignment() {
        auto *item = courses_->currentItem();
        if (!item) { QMessageBox::information(this,"Edit assignment","Select a course."); return; }
        int courseId = item->data(Qt::UserRole).toInt();
        int row = assigns_->currentRow();
        if (row < 0) { QMessageBox::information(this,"Edit assignment","Select an assignment."); return; }
        int assignId = assigns_->item(row, 0)->data(Qt::UserRole).toInt();
        AssignmentDialog ad(courseId, this, assignId);
        if (ad.exec() == QDialog::Accepted) { loadAssignments(); reloadUpcoming(); }
    }

    void deleteAssignment() {
        auto *item = courses_->currentItem();
        if (!item) { QMessageBox::information(this,"Delete assignment","Select a course."); return; }
        int row = assigns_->currentRow();
        if (row < 0) { QMessageBox::information(this,"Delete assignment","Select an assignment."); return; }
        int assignId = assigns_->item(row, 0)->data(Qt::UserRole).toInt();
        if (QMessageBox::question(this, "Delete Assignment", "Are you sure you want to delete this assignment?") == QMessageBox::Yes) {
            QSqlQuery del; del.prepare("DELETE FROM assignments WHERE id=?");
            del.addBindValue(assignId); del.exec();
            loadAssignments(); reloadUpcoming();
        }
    }

    void loadCourses() {
        courses_->clear(); if (semesterId_ < 0) return;
        QSqlQuery q; q.prepare("SELECT id, code, name FROM courses WHERE user_id=? AND semester_id=? ORDER BY code");
        q.addBindValue(userId_); q.addBindValue(semesterId_);
        if (q.exec()) while (q.next()) {
            auto *it = new QListWidgetItem(QString("%1 — %2").arg(q.value(1).toString(), q.value(2).toString()));
            it->setData(Qt::UserRole, q.value(0).toInt());
            courses_->addItem(it);
        }
        if (courses_->count() > 0) { courses_->setCurrentRow(0); loadAssignments(); }
        else { assigns_->setRowCount(0); }
    }

    void loadAssignments() {
        assigns_->setRowCount(0);
        auto *item = courses_->currentItem(); if (!item) return;
        const int courseId = item->data(Qt::UserRole).toInt();
        QSqlQuery q; q.prepare(R"(SELECT id, type, title, due_at_utc, topics
                     FROM assignments WHERE course_id=? ORDER BY due_at_utc)");
        q.addBindValue(courseId);
        if (q.exec()) { int row = 0; while (q.next()) {
            assigns_->insertRow(row);
            assigns_->setItem(row, 0, new QTableWidgetItem(q.value(1).toString()));
            assigns_->item(row, 0)->setData(Qt::UserRole, q.value(0).toInt()); // Store assignment id for edit/delete
            assigns_->setItem(row, 1, new QTableWidgetItem(q.value(2).toString()));
            const auto dt = QDateTime::fromSecsSinceEpoch(q.value(3).toLongLong()).toLocalTime();
            assigns_->setItem(row, 2, new QTableWidgetItem(QLocale().toString(dt, QLocale::ShortFormat)));
            assigns_->setItem(row, 3, new QTableWidgetItem(q.value(4).toString()));
            row++;
        }}
    }

    void reloadUpcoming() {
        upcoming_->clear(); if (semesterId_ < 0) return;
        std::priority_queue<Assignment, std::vector<Assignment>, DueSooner> pq;

        QSqlQuery q; q.prepare(R"(SELECT a.id, a.course_id, a.type, a.title, a.due_at_utc, a.topics
                     FROM assignments a
                     JOIN courses c ON a.course_id = c.id
                     WHERE c.semester_id = ? AND c.user_id = ?
                     ORDER BY a.due_at_utc)");
        q.addBindValue(semesterId_);
        q.addBindValue(userId_);
        if (q.exec()) while (q.next()) {
            Assignment a; a.id = q.value(0).toInt(); a.courseId = q.value(1).toInt();
            a.type = parseAssignType(q.value(2).toString());
            a.title = q.value(3).toString();
            a.dueAtUtc = QDateTime::fromSecsSinceEpoch(q.value(4).toLongLong()).toUTC();
            if (!q.value(5).isNull()) a.topics = q.value(5).toString();
            pq.push(a);
        }

        int shown = 0;
        while (!pq.empty() && shown < 10) {
            auto a = pq.top(); pq.pop();
            QSqlQuery qc; qc.prepare("SELECT code FROM courses WHERE id=?"); qc.addBindValue(a.courseId); qc.exec(); qc.next();
            const auto code = qc.value(0).toString();
            const auto dueLocal = QLocale().toString(a.dueAtUtc.toLocalTime(), QLocale::ShortFormat);
            auto text = QString("[%1] %2 — %3 (%4)").arg(toString(a.type), code, a.title, dueLocal);
            if (a.topics && !a.topics->isEmpty()) text += "  •  " + *a.topics;
            upcoming_->addItem(text); shown++;
        }
    }

    void loadSemesterIntoControls() {
        QSqlQuery q; q.prepare("SELECT term, year FROM semesters WHERE id=?"); q.addBindValue(semesterId_);
        if (q.exec() && q.next()) { term_->setCurrentText(q.value(0).toString()); year_->setValue(q.value(1).toInt()); }
    }

private:
    int userId_{-1}, semesterId_{-1};
    QComboBox* term_{}; QSpinBox* year_{};
    QListWidget* courses_{}; QTableWidget* assigns_{}; QListWidget* upcoming_{};
};

// Main entry point and MOC glue
int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QSqlDatabase db;
    if (!ensureDbOpen(db) || !runMigrations(db)) {
        QMessageBox::critical(nullptr, "DB Error", "Could not open or migrate SQLite DB.");
        return 1;
    }

    AuthDialog auth;
    if (auth.exec() != QDialog::Accepted || auth.userId() < 0) return 0;

    MainWindow w(auth.userId());
    w.show();
    return app.exec();
}

#include "college-course-organizer.moc"







