/*
Multistream for OBS
Copyright (C) 2026 The Visionary Society
SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "multistream-dock.hpp"
#include "destination-dialog.hpp"
#include "platforms.hpp"
#include "plugin-support.h"

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

MultistreamDock::MultistreamDock(Setup &setup, StreamEngine &engine, SaveFn save, QWidget *parent)
	: QWidget(parent),
	  setup_(setup),
	  engine_(engine),
	  save_(std::move(save))
{
	setObjectName(QStringLiteral("obsMultistreamDock"));
	setMinimumWidth(260);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(6, 6, 6, 6);

	stack_ = new QStackedWidget(this);
	root->addWidget(stack_);

	// page 0: first-run guide
	stack_->addWidget(buildWelcome());

	// page 1: destinations
	auto *page = new QWidget(stack_);
	auto *pl = new QVBoxLayout(page);
	pl->setContentsMargins(0, 0, 0, 0);

	auto *top = new QHBoxLayout();
	auto *add = new QPushButton(QStringLiteral("+ Add platform"), page);
	connect(add, &QPushButton::clicked, this, &MultistreamDock::addDestination);
	top->addWidget(add);
	top->addStretch();
	auto *more = new QToolButton(page);
	more->setText(QStringLiteral("More ▾"));
	more->setPopupMode(QToolButton::InstantPopup);
	auto *menu = new QMenu(more);
	menu->addAction(QStringLiteral("Video quality (all platforms)…"), this, &MultistreamDock::editSharedEncoder);
	menu->addSeparator();
	menu->addAction(QStringLiteral("Export my setup (no keys)…"), this, &MultistreamDock::exportSetup);
	menu->addAction(QStringLiteral("Import a setup…"), this, &MultistreamDock::importSetup);
	menu->addSeparator();
	linkMain_ = menu->addAction(QStringLiteral("Also start/stop with OBS's Start Streaming button"));
	linkMain_->setCheckable(true);
	connect(linkMain_, &QAction::toggled, this, [this](bool on) {
		if (setup_.linkMainStream != on) {
			setup_.linkMainStream = on;
			save_();
		}
	});
	more->setMenu(menu);
	top->addWidget(more);
	pl->addLayout(top);

	auto *scroll = new QScrollArea(page);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	listHost_ = new QWidget(scroll);
	listLayout_ = new QVBoxLayout(listHost_);
	listLayout_->setContentsMargins(0, 0, 0, 0);
	listLayout_->addStretch();
	scroll->setWidget(listHost_);
	pl->addWidget(scroll, 1);

	summary_ = new QLabel(page);
	summary_->setWordWrap(true);
	pl->addWidget(summary_);

	auto *bottom = new QHBoxLayout();
	startAll_ = new QPushButton(QStringLiteral("Start all"), page);
	stopAll_ = new QPushButton(QStringLiteral("Stop all"), page);
	startAll_->setMinimumHeight(30);
	stopAll_->setMinimumHeight(30);
	connect(startAll_, &QPushButton::clicked, this, &MultistreamDock::startAll);
	connect(stopAll_, &QPushButton::clicked, this, &MultistreamDock::stopAll);
	bottom->addWidget(startAll_);
	bottom->addWidget(stopAll_);
	pl->addLayout(bottom);

	stack_->addWidget(page);

	timer_ = new QTimer(this);
	timer_->setInterval(1000);
	connect(timer_, &QTimer::timeout, this, &MultistreamDock::refreshStatus);
	timer_->start();

	reload();
}

QWidget *MultistreamDock::buildWelcome()
{
	auto *w = new QWidget(this);
	auto *l = new QVBoxLayout(w);
	l->setContentsMargins(4, 4, 4, 4);

	auto *title = new QLabel(QStringLiteral("<h3>Add your first platform</h3>"), w);
	l->addWidget(title);

	auto *intro = new QLabel(QStringLiteral("Go live on several platforms at once. For each one you need two "
						"things from the platform's website: a <b>server URL</b> and your "
						"private <b>stream key</b>."),
				 w);
	intro->setWordWrap(true);
	l->addWidget(intro);

	QString links = QStringLiteral("<p>Where to find your key:</p><ul style=\"margin-left:-20px\">");
	for (const auto &p : platformPresets())
		links += QStringLiteral("<li><a href=\"%1\">%2</a></li>")
				 .arg(p.keyUrl.toHtmlEscaped(), p.name.toHtmlEscaped());
	links += QStringLiteral("</ul>");
	auto *list = new QLabel(links, w);
	list->setTextFormat(Qt::RichText);
	list->setOpenExternalLinks(true);
	list->setWordWrap(true);
	l->addWidget(list);

	auto *add = new QPushButton(QStringLiteral("+ Add a platform"), w);
	add->setMinimumHeight(32);
	connect(add, &QPushButton::clicked, this, &MultistreamDock::addDestination);
	l->addWidget(add);

	auto *imp = new QPushButton(QStringLiteral("Import a setup from a friend…"), w);
	connect(imp, &QPushButton::clicked, this, &MultistreamDock::importSetup);
	l->addWidget(imp);

	auto *note = new QLabel(QStringLiteral("<small>Keys stay on this computer. Each platform uses its own "
					       "share of your upload speed.</small>"),
				w);
	note->setWordWrap(true);
	l->addWidget(note);
	l->addStretch();
	return w;
}

Destination *MultistreamDock::find(const QString &id)
{
	for (auto &d : setup_.destinations)
		if (d.id == id)
			return &d;
	return nullptr;
}

void MultistreamDock::reload()
{
	linkMain_->blockSignals(true);
	linkMain_->setChecked(setup_.linkMainStream);
	linkMain_->blockSignals(false);
	rebuildList();
}

void MultistreamDock::rebuildList()
{
	// remove old rows (keep trailing stretch)
	while (listLayout_->count() > 1) {
		QLayoutItem *it = listLayout_->takeAt(0);
		if (it->widget())
			it->widget()->deleteLater();
		delete it;
	}
	rows_.clear();

	stack_->setCurrentIndex(setup_.destinations.isEmpty() ? 0 : 1);

	int pos = 0;
	for (const auto &d : setup_.destinations) {
		auto *row = new QFrame(listHost_);
		row->setFrameShape(QFrame::StyledPanel);
		auto *rl = new QVBoxLayout(row);
		rl->setContentsMargins(6, 4, 6, 4);
		rl->setSpacing(2);

		auto *line1 = new QHBoxLayout();
		auto *enabled = new QCheckBox(d.name, row);
		enabled->setChecked(d.enabled);
		enabled->setToolTip(QStringLiteral("Ticked = included in Start all"));
		const QString id = d.id;
		connect(enabled, &QCheckBox::toggled, this, [this, id](bool on) {
			if (Destination *x = find(id)) {
				x->enabled = on;
				save_();
				refreshStatus();
			}
		});
		line1->addWidget(enabled, 1);

		auto *toggle = new QPushButton(QStringLiteral("Start"), row);
		toggle->setToolTip(QStringLiteral("Start or stop just this platform"));
		connect(toggle, &QPushButton::clicked, this, [this, id]() { toggleOne(id); });
		line1->addWidget(toggle);

		auto *edit = new QToolButton(row);
		edit->setText(QStringLiteral("Edit"));
		connect(edit, &QToolButton::clicked, this, [this, id]() { editDestination(id); });
		line1->addWidget(edit);

		auto *del = new QToolButton(row);
		del->setText(QStringLiteral("✕"));
		del->setToolTip(QStringLiteral("Remove"));
		connect(del, &QToolButton::clicked, this, [this, id]() { removeDestination(id); });
		line1->addWidget(del);
		rl->addLayout(line1);

		auto *status = new QLabel(row);
		status->setWordWrap(true);
		status->setTextFormat(Qt::RichText);
		rl->addWidget(status);

		listLayout_->insertWidget(pos++, row);
		rows_.append({id, status, toggle});
	}
	refreshStatus();
}

void MultistreamDock::refreshStatus()
{
	int liveCount = 0, uploadKbps = 0;
	for (const Row &r : rows_) {
		const Destination *d = find(r.id);
		if (!d)
			continue;
		const PlatformPreset *p = findPreset(d->platform);
		const QString platformName = p ? p->name : QStringLiteral("Custom");
		const bool active = engine_.isActive(r.id);
		const StreamStatus st = engine_.status(r.id);

		QString text, color;
		if (active || st.state == StreamState::Failed || st.state == StreamState::Stopped ||
		    st.state == StreamState::Stopping || st.state == StreamState::Skipped) {
			text = describeStatus(st).toHtmlEscaped();
			if (st.state == StreamState::Live)
				color = QStringLiteral("#2ecc71");
			else if (st.state == StreamState::Failed)
				color = QStringLiteral("#e74c3c");
			else if (st.state == StreamState::Reconnecting || st.state == StreamState::Connecting ||
				 st.state == StreamState::Skipped)
				color = QStringLiteral("#f39c12");
		} else if (!model::hasServer(*d)) {
			text = QStringLiteral("Needs a server URL — click Edit");
			color = QStringLiteral("#f39c12");
		} else if (!model::hasKey(*d)) {
			text = QStringLiteral("Needs your stream key — click Edit");
			color = QStringLiteral("#f39c12");
		} else {
			text = d->enabled ? QStringLiteral("Ready") : QStringLiteral("Off (not in Start all)");
		}
		if (active) {
			const QString enc = engine_.encoderLabelFor(r.id);
			if (!enc.isEmpty())
				text += QStringLiteral(" · %1").arg(enc.toHtmlEscaped());
		}
		if (active) {
			liveCount++;
			uploadKbps += model::effectiveEncoder(*d, setup_.shared).videoBitrate +
				      model::effectiveEncoder(*d, setup_.shared).audioBitrate;
		}
		QString html = QStringLiteral("<small>%1 — ").arg(platformName.toHtmlEscaped());
		html += color.isEmpty() ? text : QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, text);
		html += QStringLiteral("</small>");
		r.status->setText(html);
		r.toggle->setText(active ? QStringLiteral("Stop") : QStringLiteral("Start"));
	}

	if (liveCount > 0)
		summary_->setText(QStringLiteral("<small>%1 live · using about %2 Mbps of upload</small>")
					  .arg(liveCount)
					  .arg(uploadKbps / 1000.0, 0, 'f', 1));
	else
		summary_->setText(QStringLiteral("<small>Tick the platforms you want, then press Start all.</small>"));
	stopAll_->setEnabled(engine_.anyActive());
}

void MultistreamDock::startAll()
{
	QStringList problems;
	int tried = 0;
	for (const auto &d : setup_.destinations) {
		if (!d.enabled)
			continue;
		tried++;
		QString err;
		if (!engine_.start(d, model::effectiveEncoder(d, setup_.shared), &err) && !err.isEmpty())
			problems << QStringLiteral("%1: %2").arg(d.name, err);
	}
	refreshStatus();
	if (tried == 0)
		QMessageBox::information(this, QStringLiteral("Multistream"),
					 QStringLiteral("Tick at least one platform first."));
	else if (!problems.isEmpty())
		obs_log(LOG_WARNING, "start all: %d problem(s)", (int)problems.size());
}

void MultistreamDock::stopAll()
{
	engine_.stopAll();
	refreshStatus();
}

void MultistreamDock::toggleOne(const QString &id)
{
	Destination *d = find(id);
	if (!d)
		return;
	if (engine_.isActive(id)) {
		engine_.stop(id);
	} else {
		QString err;
		if (!engine_.start(*d, model::effectiveEncoder(*d, setup_.shared), &err) && !err.isEmpty())
			QMessageBox::warning(this, d->name,
					     QStringLiteral("Couldn't start %1: %2")
						     .arg(d->name.toHtmlEscaped(), err.toHtmlEscaped()));
	}
	refreshStatus();
}

void MultistreamDock::addDestination()
{
	Destination d;
	d.id = model::newId();
	d.platform = platformPresets().first().id;
	DestinationDialog dlg(d, setup_.shared, true, false, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	setup_.destinations.append(dlg.result());
	save_();
	rebuildList();
}

void MultistreamDock::editDestination(const QString &id)
{
	Destination *d = find(id);
	if (!d)
		return;
	DestinationDialog dlg(*d, setup_.shared, false, engine_.isActive(id), this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	if ((d = find(id)) == nullptr)
		return;
	*d = dlg.result();
	save_();
	rebuildList();
}

void MultistreamDock::removeDestination(const QString &id)
{
	Destination *d = find(id);
	if (!d)
		return;
	if (engine_.isActive(id)) {
		QMessageBox::information(this, d->name, QStringLiteral("Stop this platform before removing it."));
		return;
	}
	if (QMessageBox::question(this, QStringLiteral("Remove platform"),
				  QStringLiteral("Remove \"%1\" and its saved key from this OBS profile?")
					  .arg(d->name.toHtmlEscaped())) != QMessageBox::Yes)
		return;
	engine_.forget(id);
	for (int i = 0; i < setup_.destinations.size(); i++)
		if (setup_.destinations[i].id == id) {
			setup_.destinations.removeAt(i);
			break;
		}
	save_();
	rebuildList();
}

void MultistreamDock::editSharedEncoder()
{
	SharedEncoderDialog dlg(setup_.shared, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	setup_.shared = dlg.value();
	save_();
	refreshStatus();
	if (engine_.anyActive())
		QMessageBox::information(this, QStringLiteral("Multistream"),
					 QStringLiteral("Saved. New settings apply the next time you start."));
}

void MultistreamDock::exportSetup()
{
	if (setup_.destinations.isEmpty()) {
		QMessageBox::information(this, QStringLiteral("Export"), QStringLiteral("Add a platform first."));
		return;
	}
	const QString path =
		QFileDialog::getSaveFileName(this, QStringLiteral("Export setup (no keys)"),
					     QDir::home().filePath(QStringLiteral("multistream-setup.json")),
					     QStringLiteral("Multistream setup (*.json)"));
	if (path.isEmpty())
		return;
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QMessageBox::warning(this, QStringLiteral("Export"), QStringLiteral("Couldn't save that file."));
		return;
	}
	f.write(model::toExportBytes(setup_, QString::fromUtf8(PLUGIN_VERSION)));
	f.close();
	QMessageBox::information(this, QStringLiteral("Export"),
				 QStringLiteral("Saved. This file has your platforms and quality settings but "
						"<b>no stream keys</b>, so it's safe to send to a friend. They import "
						"it and paste in their own keys."));
}

void MultistreamDock::importSetup()
{
	const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import a setup"), QDir::homePath(),
							  QStringLiteral("Multistream setup (*.json)"));
	if (path.isEmpty())
		return;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		QMessageBox::warning(this, QStringLiteral("Import"), QStringLiteral("Couldn't open that file."));
		return;
	}
	const model::ImportResult r = model::fromExportBytes(f.read(1024 * 1024 + 1));
	if (!r.ok) {
		QMessageBox::warning(this, QStringLiteral("Import"), r.error);
		return;
	}
	if (r.destinations.isEmpty()) {
		QMessageBox::information(this, QStringLiteral("Import"),
					 QStringLiteral("That setup has no platforms."));
		return;
	}

	bool replace = false;
	if (!setup_.destinations.isEmpty()) {
		QMessageBox box(QMessageBox::Question, QStringLiteral("Import"),
				QStringLiteral(
					"You already have %1 platform(s). Add the imported ones to your list, or "
					"replace your list?")
					.arg(setup_.destinations.size()),
				QMessageBox::NoButton, this);
		QPushButton *addBtn = box.addButton(QStringLiteral("Add to my list"), QMessageBox::AcceptRole);
		QPushButton *repBtn = box.addButton(QStringLiteral("Replace my list"), QMessageBox::DestructiveRole);
		box.addButton(QMessageBox::Cancel);
		box.exec();
		if (box.clickedButton() == repBtn)
			replace = true;
		else if (box.clickedButton() != addBtn)
			return;
		if (replace && engine_.anyActive()) {
			QMessageBox::information(this, QStringLiteral("Import"),
						 QStringLiteral("Stop streaming before replacing your list."));
			return;
		}
	}

	if (replace) {
		for (const auto &d : setup_.destinations)
			engine_.forget(d.id);
		setup_.destinations.clear();
	}
	setup_.destinations.append(r.destinations);
	if (r.hasShared && (replace || setup_.destinations.size() == r.destinations.size()))
		setup_.shared = r.shared;
	save_();
	rebuildList();
	QMessageBox::information(this, QStringLiteral("Import"),
				 QStringLiteral("Imported %1 platform(s). Click <b>Edit</b> on each one and paste your "
						"own stream key (and server URL where it asks).")
					 .arg(r.destinations.size()));
}
