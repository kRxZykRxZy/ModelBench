import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 820
    minimumWidth: 960
    minimumHeight: 640
    title: "ModelBench"
    color: Theme.bg

    property string currentView: "home"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.preferredWidth: compact ? 64 : 240
            Layout.fillHeight: true
            compact: window.width < 1100
            currentView: Navigation.currentView
            onViewSelected: (v) => Navigation.go(v)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            HeaderBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                currentView: Navigation.currentView
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: viewIndex(Navigation.currentView)

                HomeView { }
                ChatView { }
                ModelsView { }
                EngineInstallView { }
                McpHubView { }
                SkillsHubView { }
                SettingsView { }
                LoginView { }
                SubscriptionView { }
                GitView { }
                CreditsView { }
                OnboardingWizard { }
            }
        }
    }

    function viewIndex(v) {
        switch (v) {
            case "home": return 0;
            case "chat": return 1;
            case "models": return 2;
            case "engine": return 3;
            case "mcp": return 4;
            case "skills": return 5;
            case "settings": return 6;
            case "account": return 7;
            case "subscription": return 8;
            case "git": return 9;
            case "credits": return 10;
            case "onboarding": return 11;
        }
        return 0;
    }
}
