pragma Singleton

import QtQuick

ListModel {
    ListElement { bandId: 0; centerHz: 3.0e9; widthHz: 5.0e8; thresholdDb: -80; enabled: true }
    ListElement { bandId: 1; centerHz: 5.795e9; widthHz: 4.10e8; thresholdDb: -85; enabled: true }
    ListElement { bandId: 2; centerHz: 8.25e9; widthHz: 5.0e8; thresholdDb: -78; enabled: true }
    ListElement { bandId: 3; centerHz: 9.55e9; widthHz: 5.0e8; thresholdDb: -90; enabled: true }
    ListElement { bandId: 4; centerHz: 1.425e10; widthHz: 5.0e8; thresholdDb: -82; enabled: true }
}
