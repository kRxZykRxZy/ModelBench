pragma Singleton
import QtQuick

QtObject {
    id: nav
    property string currentView: "home"
    property int windowWidth: 1280

    function go(view) { nav.currentView = view }
}
