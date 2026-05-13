pragma Singleton

import QtQuick

ListModel {
    ListElement {
        bandId: 0
        centerHz: 3.0e9
        widthHz: 5.0e8
        thresholdAmplitude: 180
        enabled: true
        color: "#4BB4FF"
        borderColor: "#7FCBFF"
        textColor: "#DDF4FF"
    }
    ListElement {
        bandId: 1
        centerHz: 5.795e9
        widthHz: 4.10e8
        thresholdAmplitude: 160
        enabled: true
        color: "#35D07F"
        borderColor: "#7CF3A9"
        textColor: "#E3FFF0"
    }
    ListElement {
        bandId: 2
        centerHz: 8.25e9
        widthHz: 5.0e8
        thresholdAmplitude: 190
        enabled: true
        color: "#E5B84B"
        borderColor: "#FFD671"
        textColor: "#FFF2C5"
    }
    ListElement {
        bandId: 3
        centerHz: 9.55e9
        widthHz: 5.0e8
        thresholdAmplitude: 140
        enabled: true
        color: "#E46BD4"
        borderColor: "#FF9AF0"
        textColor: "#FFE0FA"
    }
    ListElement {
        bandId: 4
        centerHz: 1.425e10
        widthHz: 5.0e8
        thresholdAmplitude: 170
        enabled: true
        color: "#8A7CFF"
        borderColor: "#B6AEFF"
        textColor: "#ECE9FF"
    }
}
