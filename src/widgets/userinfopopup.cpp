#include "userinfopopup.hpp"

#include "application.hpp"
#include "providers/twitch/twitchchannel.hpp"
#include "singletons/resourcemanager.hpp"
#include "util/layoutcreator.hpp"
#include "util/posttothread.hpp"
#include "util/urlfetch.hpp"
#include "widgets/helper/line.hpp"
#include "widgets/helper/rippleeffectlabel.hpp"

#include <QCheckBox>
#include <QDesktopServices>
#include <QLabel>

#define TEXT_FOLLOWERS "Followers: "
#define TEXT_VIEWS "Views: "
#define TEXT_CREATED "Created: "

namespace chatterino {
namespace widgets {

UserInfoPopup::UserInfoPopup()
    : BaseWindow(nullptr, BaseWindow::Flags(BaseWindow::Frameless | BaseWindow::DeleteOnFocusOut))
    , hack_(new bool)
{
    auto app = getApp();

    auto layout = util::LayoutCreator<UserInfoPopup>(this).setLayoutType<QVBoxLayout>();

    // first line
    auto head = layout.emplace<QHBoxLayout>().withoutMargin();
    {
        // avatar
        //        auto avatar = head.emplace<QLabel>("Avatar").assign(&this->ui_.avatarButtoAn);
        auto avatar = head.emplace<RippleEffectButton>(nullptr).assign(&this->ui_.avatarButton);
        avatar->setFixedSize(100, 100);
        QObject::connect(*avatar, &RippleEffectButton::clicked, [this] {
            QDesktopServices::openUrl(QUrl("https://twitch.tv/" + this->userName_));
        });

        // items on the right
        auto vbox = head.emplace<QVBoxLayout>();
        {
            auto name = vbox.emplace<QLabel>().assign(&this->ui_.nameLabel);

            auto font = name->font();
            font.setBold(true);
            name->setFont(font);
            vbox.emplace<QLabel>(TEXT_VIEWS).assign(&this->ui_.viewCountLabel);
            vbox.emplace<QLabel>(TEXT_FOLLOWERS).assign(&this->ui_.followerCountLabel);
            vbox.emplace<QLabel>(TEXT_CREATED).assign(&this->ui_.createdDateLabel);
        }
    }

    layout.emplace<Line>(false);

    // second line
    auto user = layout.emplace<QHBoxLayout>().withoutMargin();
    {
        user->addStretch(1);

        user.emplace<QCheckBox>("Follow").assign(&this->ui_.follow);
        user.emplace<QCheckBox>("Ignore").assign(&this->ui_.ignore);
        user.emplace<QCheckBox>("Ignore highlights").assign(&this->ui_.ignoreHighlights);

        auto mod = user.emplace<RippleEffectButton>(this);
        mod->setPixmap(app->resources->buttons.mod);
        mod->setScaleIndependantSize(30, 30);
        auto unmod = user.emplace<RippleEffectLabel>();
        unmod->setPixmap(app->resources->buttons.unmod);
        unmod->setScaleIndependantSize(30, 30);

        user->addStretch(1);

        // userstate
        this->userStateChanged.connect([this, mod, unmod]() mutable {
            providers::twitch::TwitchChannel *twitchChannel =
                dynamic_cast<providers::twitch::TwitchChannel *>(this->channel_.get());

            if (twitchChannel) {
                qDebug() << this->userName_;

                bool isMyself =
                    QString::compare(getApp()->accounts->twitch.getCurrent()->getUserName(),
                                     this->userName_, Qt::CaseInsensitive) == 0;

                mod->setVisible(twitchChannel->isBroadcaster() && !isMyself);
                unmod->setVisible((twitchChannel->isBroadcaster() && !isMyself) ||
                                  (twitchChannel->isMod() && isMyself));
            }
        });
    }

    auto lineMod = layout.emplace<Line>(false);

    // third line
    auto moderation = layout.emplace<QHBoxLayout>().withoutMargin();
    {
        auto timeout = moderation.emplace<TimeoutWidget>();

        this->userStateChanged.connect([this, lineMod, timeout]() mutable {
            providers::twitch::TwitchChannel *twitchChannel =
                dynamic_cast<providers::twitch::TwitchChannel *>(this->channel_.get());

            if (twitchChannel) {
                lineMod->setVisible(twitchChannel->hasModRights());
                timeout->setVisible(twitchChannel->hasModRights());
            }
        });
    }

    this->setStyleSheet("font-size: 11pt;");

    this->installEvents();
}

void UserInfoPopup::installEvents()
{
    std::weak_ptr<bool> hack = this->hack_;

    // follow
    QObject::connect(this->ui_.follow, &QCheckBox::stateChanged, [this](int) mutable {
        auto currentUser = getApp()->accounts->twitch.getCurrent();

        QUrl requestUrl("https://api.twitch.tv/kraken/users/" + currentUser->getUserId() +
                        "/follows/channels/" + this->userId_);

        this->ui_.follow->setEnabled(false);
        if (this->ui_.follow->isChecked()) {
            util::twitch::put(requestUrl,
                              [this](QJsonObject) { this->ui_.follow->setEnabled(true); });
        } else {
            util::twitch::sendDelete(requestUrl, [this] { this->ui_.follow->setEnabled(true); });
        }
    });

    std::shared_ptr<bool> ignoreNext = std::make_shared<bool>(false);

    // ignore
    QObject::connect(
        this->ui_.ignore, &QCheckBox::stateChanged, [this, ignoreNext, hack](int) mutable {
            if (*ignoreNext) {
                *ignoreNext = false;
                return;
            }

            this->ui_.ignore->setEnabled(false);

            auto currentUser = getApp()->accounts->twitch.getCurrent();
            if (this->ui_.ignore->isChecked()) {
                currentUser->ignoreByID(this->userId_, this->userName_,
                                        [=](auto result, const auto &message) mutable {
                                            if (hack.lock()) {
                                                if (result == IgnoreResult_Failed) {
                                                    *ignoreNext = true;
                                                    this->ui_.ignore->setChecked(false);
                                                }
                                                this->ui_.ignore->setEnabled(true);
                                            }
                                        });
            } else {
                currentUser->unignoreByID(this->userId_, this->userName_,
                                          [=](auto result, const auto &message) mutable {
                                              if (hack.lock()) {
                                                  if (result == UnignoreResult_Failed) {
                                                      *ignoreNext = true;
                                                      this->ui_.ignore->setChecked(true);
                                                  }
                                                  this->ui_.ignore->setEnabled(true);
                                              }
                                          });
            }
        });
}

void UserInfoPopup::setData(const QString &name, const ChannelPtr &channel)
{
    this->userName_ = name;
    this->channel_ = channel;

    this->ui_.nameLabel->setText(name);

    this->updateUserData();

    this->userStateChanged.invoke();
}

void UserInfoPopup::updateUserData()
{
    std::weak_ptr<bool> hack = this->hack_;

    // get user info
    util::twitch::getUserID(this->userName_, this, [this, hack](QString id) {
        auto currentUser = getApp()->accounts->twitch.getCurrent();

        this->userId_ = id;

        // get channel info
        util::twitch::get(
            "https://api.twitch.tv/kraken/channels/" + id, this, [this](const QJsonObject &obj) {
                this->ui_.followerCountLabel->setText(
                    TEXT_FOLLOWERS + QString::number(obj.value("followers").toInt()));
                this->ui_.viewCountLabel->setText(TEXT_VIEWS +
                                                  QString::number(obj.value("views").toInt()));
                this->ui_.createdDateLabel->setText(
                    TEXT_CREATED + obj.value("created_at").toString().section("T", 0, 0));

                this->loadAvatar(QUrl(obj.value("logo").toString()));
            });

        // get follow state
        currentUser->checkFollow(id, [this, hack](auto result) {
            if (hack.lock()) {
                if (result != FollowResult_Failed) {
                    this->ui_.follow->setEnabled(true);
                    this->ui_.follow->setChecked(result == FollowResult_Following);
                }
            }
        });

        // get ignore state
        bool isIgnoring = false;
        for (const auto &ignoredUser : currentUser->getIgnores()) {
            if (id == ignoredUser.id) {
                isIgnoring = true;
                break;
            }
        }

        this->ui_.ignore->setEnabled(true);
        this->ui_.ignore->setChecked(isIgnoring);
    });

    this->ui_.follow->setEnabled(false);
    this->ui_.ignore->setEnabled(false);
    this->ui_.ignoreHighlights->setEnabled(false);
}

void UserInfoPopup::loadAvatar(const QUrl &url)
{
    QNetworkRequest req(url);
    static auto manager = new QNetworkAccessManager();
    auto *reply = manager->get(req);

    QObject::connect(reply, &QNetworkReply::finished, this, [=] {
        if (reply->error() == QNetworkReply::NoError) {
            const auto data = reply->readAll();

            // might want to cache the avatar image
            QPixmap avatar;
            avatar.loadFromData(data);
            this->ui_.avatarButton->setPixmap(avatar);
        } else {
            this->ui_.avatarButton->setPixmap(QPixmap());
        }
    });
}

//
// TimeoutWidget
//
UserInfoPopup::TimeoutWidget::TimeoutWidget()
    : BaseWidget(nullptr)
{
    auto layout =
        util::LayoutCreator<TimeoutWidget>(this).setLayoutType<QHBoxLayout>().withoutMargin();

    QColor color1(255, 255, 255, 80);
    QColor color2(255, 255, 255, 0);

    int buttonWidth = 40;
    int buttonWidth2 = 24;
    int buttonHeight = 32;

    layout->setSpacing(16);

    {
        auto vbox = layout.emplace<QVBoxLayout>().withoutMargin();
        {
            auto title = vbox.emplace<QHBoxLayout>().withoutMargin();
            title->addStretch(1);
            auto label = title.emplace<QLabel>("unban");
            label->setStyleSheet("color: #BBB");
            title->addStretch(1);

            auto hbox = vbox.emplace<QHBoxLayout>().withoutMargin();
            hbox->setSpacing(0);
            {
                auto unban = hbox.emplace<RippleEffectButton>(nullptr);
                unban->setPixmap(getApp()->resources->buttons.unban);
                unban->setScaleIndependantSize(buttonHeight, buttonHeight);
                unban->setBorderColor(QColor(255, 255, 255, 127));
            }
        }
    }

    {
        auto vbox = layout.emplace<QVBoxLayout>().withoutMargin();
        {
            auto title = vbox.emplace<QHBoxLayout>().withoutMargin();
            title->addStretch(1);
            auto label = title.emplace<QLabel>("sec");
            label->setStyleSheet("color: #BBB");
            title->addStretch(1);

            auto hbox = vbox.emplace<QHBoxLayout>().withoutMargin();
            hbox->setSpacing(0);
            {
                auto a = hbox.emplace<RippleEffectLabel>();
                a->getLabel().setText("1");
                a->setScaleIndependantSize(buttonWidth2, buttonHeight);
                a->setBorderColor(color1);
            }
        }
    }

    {
        auto vbox = layout.emplace<QVBoxLayout>().withoutMargin();
        {
            auto title = vbox.emplace<QHBoxLayout>().withoutMargin();
            title->addStretch(1);
            auto label = title.emplace<QLabel>("min");
            label->setStyleSheet("color: #BBB");
            title->addStretch(1);

            auto hbox = vbox.emplace<QHBoxLayout>().withoutMargin();
            hbox->setSpacing(0);
            {
                auto a = hbox.emplace<RippleEffectLabel>();
                a->getLabel().setText("1");
                a->setScaleIndependantSize(buttonWidth2, buttonHeight);
                a->setBorderColor(color1);
            }
            {
                auto a = hbox.emplace<RippleEffectLabel>();
                a->getLabel().setText("5");
                a->setScaleIndependantSize(buttonWidth2, buttonHeight);
                a->setBorderColor(color1);
            }
            {
                auto a = hbox.emplace<RippleEffectLabel>();
                a->getLabel().setText("10");
                a->setScaleIndependantSize(buttonWidth, buttonHeight);
                a->setBorderColor(color1);
            }
        }
    }

    {
        auto vbox = layout.emplace<QVBoxLayout>().withoutMargin();
        {
            auto title = vbox.emplace<QHBoxLayout>().withoutMargin();
            title->addStretch(1);
            auto label = title.emplace<QLabel>("hour");
            label->setStyleSheet("color: #BBB");
            title->addStretch(1);

            auto hbox = vbox.emplace<QHBoxLayout>().withoutMargin();
            hbox->setSpacing(0);
            {
                auto a = hbox.emplace<RippleEffectLabel>();
                a->getLabel().setText("1");
                a->setScaleIndependantSize(buttonWidth2, buttonHeight);
                a->setBorderColor(color1);
            }
            {
                auto a = hbox.emplace<RippleEffectLabel>();
                a->getLabel().setText("4");
                a->setScaleIndependantSize(buttonWidth2, buttonHeight);
                a->setBorderColor(color1);
            }
        }
    }

    {
        auto vbox = layout.emplace<QVBoxLayout>().withoutMargin();
        {
            auto title = vbox.emplace<QHBoxLayout>().withoutMargin();
            title->addStretch(1);
            auto label = title.emplace<QLabel>("week");
            label->setStyleSheet("color: #BBB");
            title->addStretch(1);

            auto hbox = vbox.emplace<QHBoxLayout>().withoutMargin();
            hbox->setSpacing(0);
            {
                auto a = hbox.emplace<RippleEffectLabel>();
                a->getLabel().setText("1");
                a->setScaleIndependantSize(buttonWidth2, buttonHeight);
                a->setBorderColor(color1);
            }
            {
                auto a = hbox.emplace<RippleEffectLabel>();
                a->getLabel().setText("2");
                a->setScaleIndependantSize(buttonWidth2, buttonHeight);
                a->setBorderColor(color1);
            }
        }
    }

    {
        auto vbox = layout.emplace<QVBoxLayout>().withoutMargin();
        {
            auto title = vbox.emplace<QHBoxLayout>().withoutMargin();
            title->addStretch(1);
            auto label = title.emplace<QLabel>("ban");
            label->setStyleSheet("color: #BBB");
            title->addStretch(1);

            auto hbox = vbox.emplace<QHBoxLayout>().withoutMargin();
            hbox->setSpacing(0);
            {
                auto ban = hbox.emplace<RippleEffectButton>(nullptr);
                ban->setPixmap(getApp()->resources->buttons.ban);
                ban->setScaleIndependantSize(buttonHeight, buttonHeight);
                ban->setBorderColor(QColor(255, 255, 255, 127));
            }
        }
    }
}

void UserInfoPopup::TimeoutWidget::paintEvent(QPaintEvent *)
{
    //    QPainter painter(this);

    //    painter.setPen(QColor(255, 255, 255, 63));

    //    painter.drawLine(0, this->height() / 2, this->width(), this->height() / 2);
}

}  // namespace widgets
}  // namespace chatterino
