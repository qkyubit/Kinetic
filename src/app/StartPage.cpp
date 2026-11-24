#include "StartPage.h"

#include "Icons.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace kn {

QString exampleTitle(const QString& fileName)
{
    QString title = QFileInfo(fileName).completeBaseName();
    title.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!title.isEmpty() && title[0].isDigit()) title = title.section(QLatin1Char(' '), 1); // "01 pendulum"
    if (!title.isEmpty()) title[0] = title[0].toUpper();
    return title;
}

QStringList exampleFiles()
{
    return QDir(QStringLiteral(":/examples")).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
}

QStringList recentFiles()
{
    QStringList l = QSettings().value(QStringLiteral("recentFiles")).toStringList();
    l.erase(std::remove_if(l.begin(), l.end(), [](const QString& p) { return !QFileInfo::exists(p); }), l.end());
    return l;
}

void addRecentFile(const QString& path)
{
    QSettings s;
    QStringList l = s.value(QStringLiteral("recentFiles")).toStringList();
    const QString abs = QFileInfo(path).absoluteFilePath();
    l.removeAll(abs);
    l.prepend(abs);
    while (l.size() > 10) l.removeLast();
    s.setValue(QStringLiteral("recentFiles"), l);
}

StartPage::StartPage(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("startPage"));
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(40, 32, 40, 24);
    outer->setSpacing(32);

    auto* left = new QVBoxLayout;
    auto* logo = new QLabel(this);
    logo->setPixmap(QIcon(QStringLiteral(":/io.github.qkyubit.Kinetic.svg")).pixmap(96, 96));
    left->addWidget(logo);
    auto* title = new QLabel(tr("Kinetic"), this);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 2.2);
    f.setWeight(QFont::DemiBold);
    title->setFont(f);
    left->addWidget(title);
    auto* sub = new QLabel(tr("A 2D physics sandbox: build a scene, press Play, measure what happens."), this);
    sub->setWordWrap(true);
    left->addWidget(sub);
    left->addSpacing(16);
    auto* newBtn = new QPushButton(themedIcon(QStringLiteral("document-new"), QStringLiteral(":/icons/tool.svg")),
                                   tr("New Scene"), this);
    auto* openBtn = new QPushButton(themedIcon(QStringLiteral("document-open"), QStringLiteral(":/icons/tool.svg")),
                                    tr("Open Scene…"), this);
    for (QPushButton* b : {newBtn, openBtn}) {
        b->setMinimumHeight(34);
        left->addWidget(b);
    }
    connect(newBtn, &QPushButton::clicked, this, &StartPage::newSceneRequested);
    connect(openBtn, &QPushButton::clicked, this, &StartPage::openRequested);
    left->addSpacing(16);
    left->addWidget(new QLabel(tr("<b>Recent scenes</b>"), this));
    recent_ = new QListWidget(this);
    recent_->setFrameShape(QFrame::NoFrame);
    recent_->setMaximumWidth(360);
    connect(recent_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* it) { emit fileRequested(it->data(Qt::UserRole).toString()); });
    left->addWidget(recent_, 1);
    showAtStart_ = new QCheckBox(tr("Show this page at start-up"), this);
    showAtStart_->setChecked(QSettings().value(QStringLiteral("startPage/show"), true).toBool());
    connect(showAtStart_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue(QStringLiteral("startPage/show"), on); });
    left->addWidget(showAtStart_);
    outer->addLayout(left, 2);

    auto* right = new QVBoxLayout;
    right->addWidget(new QLabel(tr("<b>Examples</b>"), this));
    examples_ = new QListWidget(this);
    examples_->setViewMode(QListView::IconMode);
    examples_->setFlow(QListView::LeftToRight);
    examples_->setWrapping(true);
    examples_->setResizeMode(QListView::Adjust);
    examples_->setMovement(QListView::Static);
    examples_->setIconSize(QSize(40, 40));
    examples_->setGridSize(QSize(170, 96));
    examples_->setWordWrap(true);
    examples_->setUniformItemSizes(true);
    examples_->setFrameShape(QFrame::NoFrame);
    connect(examples_, &QListWidget::itemActivated, this,
            [this](QListWidgetItem* it) { emit exampleRequested(it->data(Qt::UserRole).toString()); });
    right->addWidget(examples_, 1);
    outer->addLayout(right, 3);
    refresh();
}

void StartPage::refresh()
{
    recent_->clear();
    for (const QString& p : recentFiles()) {
        auto* it = new QListWidgetItem(themedIcon(QStringLiteral("text-x-generic"), QStringLiteral(":/icons/tool.svg")),
                                       QFileInfo(p).fileName(), recent_);
        it->setToolTip(p);
        it->setData(Qt::UserRole, p);
    }
    if (recent_->count() == 0) {
        auto* it = new QListWidgetItem(tr("(none yet)"), recent_);
        it->setFlags(Qt::NoItemFlags);
    }
    examples_->clear();
    for (const QString& f : exampleFiles()) {
        // Pick an icon from the example's subject.
        const QString l = f.toLower();
        QString icon = QStringLiteral(":/icons/body.svg");
        if (l.contains(QLatin1String("optic"))) icon = QStringLiteral(":/icons/optics.svg");
        else if (l.contains(QLatin1String("buoy"))) icon = QStringLiteral(":/icons/fluid.svg");
        else if (l.contains(QLatin1String("charge")) || l.contains(QLatin1String("field")) || l.contains(QLatin1String("orbit")))
            icon = QStringLiteral(":/icons/field.svg");
        else if (l.contains(QLatin1String("equation"))) icon = QStringLiteral(":/icons/equation.svg");
        else if (l.contains(QLatin1String("pendulum")) || l.contains(QLatin1String("spring")) || l.contains(QLatin1String("crank")) ||
                 l.contains(QLatin1String("atwood")) || l.contains(QLatin1String("gear")))
            icon = QStringLiteral(":/icons/connector.svg");
        auto* it = new QListWidgetItem(QIcon(icon), exampleTitle(f), examples_);
        it->setData(Qt::UserRole, f);
        it->setToolTip(exampleTitle(f));
    }
}

} // namespace kn
