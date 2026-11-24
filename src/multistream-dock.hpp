/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "destinations.hpp"
#include "stream-engine.hpp"

#include <QWidget>

#include <functional>

class QAction;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

class MultistreamDock : public QWidget {
public:
	using SaveFn = std::function<void()>;

	MultistreamDock(Setup &setup, StreamEngine &engine, SaveFn save, QWidget *parent = nullptr);

	// Called after the profile's config was (re)loaded.
	void reload();
	void startAll();
	void stopAll();

private:
	void rebuildList();
	void refreshStatus();
	void addDestination();
	void editDestination(const QString &id);
	void removeDestination(const QString &id);
	void toggleOne(const QString &id);
	void importSetup();
	void exportSetup();
	void editSharedEncoder();
	Destination *find(const QString &id);
	QWidget *buildWelcome();

	Setup &setup_;
	StreamEngine &engine_;
	SaveFn save_;

	QStackedWidget *stack_ = nullptr;
	QWidget *listHost_ = nullptr;
	QVBoxLayout *listLayout_ = nullptr;
	QPushButton *startAll_ = nullptr;
	QPushButton *stopAll_ = nullptr;
	QLabel *summary_ = nullptr;
	QAction *linkMain_ = nullptr;
	QTimer *timer_ = nullptr;

	struct Row {
		QString id;
		QLabel *status;
		QPushButton *toggle;
	};
	QList<Row> rows_;
};
