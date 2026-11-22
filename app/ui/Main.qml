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
    title: "ModelFit"
    color: Theme.bg

    property string currentView: "home"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.preferredWidth: compact ? 64 : 240
            Layout.fillHeight: true
            compact: window.width < 1100
            currentView: window.currentView
            onViewSelected: (v) => window.currentView = v
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            HeaderBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                currentView: window.currentView
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: viewIndex(window.currentView)

                HomeView { }
                ChatView { }
                ModelsView { }
                EngineInstallView { }
                CalculatorView { }
                McpHubView { }
                SkillsHubView { }
                SettingsView { }
                SubscriptionView { }
            }
        }
    }

    function viewIndex(v) {
        switch (v) {
            case "home": return 0;
            case "chat": return 1;
            case "models": return 2;
            case "engine": return 3;
            case "calculator": return 4;
            case "mcp": return 5;
            case "skills": return 6;
            case "settings": return 7;
            case "subscription": return 8;
        }
        return 0;
    }
}
