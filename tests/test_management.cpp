#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QSpinBox>
#include <QListWidget>
#include <QComboBox>
#include <QDialog>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QMessageBox>
#include <QMenu>
#include "core/MoveTransition.h"
#include "core/PanelProtocol.h"
#include "config/Configuration.h"
#include "config/ConfigurationMerge.h"
#include "core/AmcpClient.h"
#include "core/SwitcherEngine.h"
#include "ui/SetupWorkspace.h"
#include "ui/SuperSourceEditor.h"
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QDoubleSpinBox>
#include "ui/MainWindow.h"

class TestManagement : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_configRoot;
private slots:
    void dustMixPreparationRoundtrip(){
        Configuration config;config.setDustMix(75,3,10);Configuration restored;restored.fromJson(config.toJson());
        QCOMPARE(restored.dustRatio(),75);QCOMPARE(restored.dustSize(),3);QCOMPARE(restored.dustFlash(),10);
        restored.setDustMix(100,0,100);QCOMPARE(restored.dustSize(),3);
        config.setWipeBorderProfile(-1,15,0);restored.fromJson(config.toJson());QCOMPARE(restored.wipeBorderSide(),-1);QCOMPARE(restored.wipeInnerSoft(),15);QCOMPARE(restored.wipeOuterSoft(),0);restored.setWipeBorderProfile(2,20,30);QCOMPARE(restored.wipeBorderSide(),-1);
        Transition transition;QVERIFY(Transition::namedMix("dustmix",25,&transition));QCOMPARE(transition.type,TransitionType::DustMix);QCOMPARE(transition.typeName(),QString("dustmix"));
    }

    void initTestCase() {
        QVERIFY(m_configRoot.isValid());
        qputenv("XDG_CONFIG_HOME", m_configRoot.path().toUtf8());
        QVERIFY(Configuration().configFilePath().startsWith(m_configRoot.path()));
    }

    void previousNameConfigurationMigration() {
        const QString canonical = Configuration().configFilePath();
        QVERIFY(canonical.contains("/kavtor/"));
        QVERIFY(!QFile::exists(canonical));
        const QString legacy = m_configRoot.path() + "/strata/switcher.json";
        QDir().mkpath(QFileInfo(legacy).absolutePath());
        Configuration old;old.setConfigFilePath(legacy);old.setAutoDurationFrames(47);old.setCasparHost("192.0.2.8");QVERIFY(old.save());
        Configuration imported;QVERIFY(imported.load());QCOMPARE(imported.autoDurationFrames(),47);QCOMPARE(imported.casparHost(),QString("192.0.2.8"));QVERIFY(QFile::exists(legacy));QVERIFY(QFile::exists(canonical));
        imported.setAutoDurationFrames(62);QVERIFY(imported.save());
        old.setAutoDurationFrames(19);QVERIFY(old.save());Configuration current;QVERIFY(current.load());QCOMPARE(current.autoDurationFrames(),62);
        QVERIFY(QFile::remove(canonical));QVERIFY(QFile::remove(legacy));
    }

    void panelSettingsSurvivePreparationSave() {
        QTemporaryDir dir; Configuration config;
        config.setConfigFilePath(dir.filePath("merged.json"));
        QTcpServer reservation; QVERIFY(reservation.listen(QHostAddress::LocalHost));
        config.setPanelPort(reservation.serverPort()); reservation.close();
        AmcpClient client; SwitcherEngine engine(&config, &client);
        PanelProtocol panel(&engine); MainWindow window(&engine, &panel);
        auto* table = window.findChild<QTableWidget*>("sourceMatrix"); QVERIFY(table);
        table->item(0,2)->setText("Prepared camera name");
        config.setAutoDurationFrames(73);
        config.setDmeBackground("cube",1002);config.setDmeBackground("global",2);
        window.findChild<QPushButton*>("saveApplyButton")->click();
        Configuration saved; saved.setConfigFilePath(config.configFilePath()); QVERIFY(saved.load());
        QCOMPARE(saved.autoDurationFrames(),73);QCOMPARE(saved.dmeBackground("global"),2);QCOMPARE(saved.dmeBackground("cube"),1002);QVERIFY(saved.dmeBackgroundCustom("cube"));QCOMPARE(saved.dmeBackground("page_roll"),2);
        QCOMPARE(saved.toJson()["transitions"].toObject()["dmeBackgrounds"], config.toJson()["transitions"].toObject()["dmeBackgrounds"]);
        QCOMPARE(saved.sourceById(0)->name,QString("Prepared camera name"));
        config.setAutoDurationFrames(91);
        window.findChild<QPushButton*>("applyPrepared")->click();
        QCOMPARE(config.autoDurationFrames(),91);
        QCOMPARE(config.sourceById(0)->name,QString("Prepared camera name"));
    }

    void concurrentConfigurationConflict() {
        QStringList conflicts;
        QJsonObject base{{"transitions",QJsonObject{{"autoDurationFrames",25},{"wipeBorder",0}}}};
        QJsonObject draft{{"transitions",QJsonObject{{"autoDurationFrames",50},{"wipeBorder",0}}}};
        QJsonObject live{{"transitions",QJsonObject{{"autoDurationFrames",75},{"wipeBorder",20}}}};
        const auto merged=mergeConfiguration(base,draft,live,conflicts).toObject();
        QCOMPARE(conflicts,QStringList{"/transitions/autoDurationFrames"});
        QCOMPARE(merged["transitions"].toObject()["wipeBorder"].toInt(),20);
    }

    void clipLoopSettings() {
        Configuration config;
        auto clip = *config.sourceById(0);
        clip.type = SourceType::File; clip.argument = "Background clip.mov";
        QVERIFY(clip.loop);
        QVERIFY(clip.producerCommand().contains(" LOOP VF "));
        clip.loop = false; config.setSource(clip);
        QVERIFY(!clip.producerCommand().contains(" LOOP "));
        QVERIFY(clip.producerCommand().contains(" VF "));
        QTemporaryDir dir; config.setConfigFilePath(dir.filePath("clips.json"));
        QVERIFY(config.save());
        Configuration loaded; loaded.setConfigFilePath(config.configFilePath());
        QVERIFY(loaded.load()); QVERIFY(!loaded.sourceById(0)->loop);
        auto legacy = config.toJson(); auto sources = legacy["sources"].toArray();
        auto input = sources[0].toObject(); input.remove("loop"); sources[0] = input;
        legacy["sources"] = sources; Configuration old; old.fromJson(legacy);
        QVERIFY(old.sourceById(0)->loop);
        SetupWorkspace workspace(&loaded);
        auto* table = workspace.findChild<QTableWidget*>("sourceMatrix"); QVERIFY(table);
        auto* loop = qobject_cast<QCheckBox*>(table->cellWidget(0,8)); QVERIFY(loop);
        QVERIFY(loop->isEnabled()); QVERIFY(!loop->isChecked());
        loop->setChecked(true);
        QVERIFY(workspace.isDirty());
        QVERIFY(!loaded.sourceById(0)->loop); // Preparation remains a draft.
        Configuration applied; QVERIFY(workspace.applyTo(&applied));
        QVERIFY(applied.sourceById(0)->loop);
        auto* type = qobject_cast<QComboBox*>(table->cellWidget(0,3));
        type->setCurrentIndex(5); // Still: loop is not a producer option.
        QVERIFY(!loop->isEnabled());
        QVERIFY(!applied.sourceById(0)->producerCommand().isEmpty());
        for (int reserved : {11,23}) QVERIFY(!table->cellWidget(reserved,8)->isEnabled());
    }

    void clipLoopPendingApply() {
        QTemporaryDir dir; Configuration config;
        config.setConfigFilePath(dir.filePath("loop.json"));
        auto clip = *config.sourceById(0); clip.type = SourceType::File;
        clip.argument = "clip.mov"; clip.enabled = true; config.setSource(clip);
        AmcpClient client; SwitcherEngine engine(&config, &client);
        PanelProtocol panel(&engine); MainWindow window(&engine, &panel);
        auto* table = window.findChild<QTableWidget*>("sourceMatrix"); QVERIFY(table);
        auto* loop = qobject_cast<QCheckBox*>(table->cellWidget(0,8)); QVERIFY(loop);
        loop->setChecked(false);
        window.findChild<QPushButton*>("saveApplyButton")->click();
        QCOMPARE(table->item(0,7)->text(), QString("Pending apply"));
        QVERIFY(config.sourceById(0)->loop);
        QVERIFY(window.findChild<QPushButton*>("applyPrepared")->isEnabled());
    }

    void independentClipTransportIcons() {
        Configuration config;NativeMultiview mv;mv.transportIcons=true;mv.ready=true;mv.names=QStringList{"Clip A","Clip B"};mv.vacant=QVector<bool>(24,false);mv.clocks.resize(24);
        mv.clocks[0].duration=100;mv.clocks[0].elapsed=10;mv.clocks[0].playbackKnown=true;
        mv.clocks[1].duration=200;mv.clocks[1].elapsed=25;mv.clocks[1].playbackKnown=true;mv.clocks[1].paused=true;
        auto nodes=mv.scene(config)["nodes"].toArray();int right=0,left=0;
        for(auto v:nodes)if(v.toObject()["type"]=="triangle"){right+=v.toObject()["direction"]=="right";left+=v.toObject()["direction"]=="left";}
        QCOMPARE(right,1);QCOMPARE(left,0);
        mv.clocks[1].scrubDirection=-1;mv.clocks[1].scrubUntilMs=clipMonotonicMs()+1000;nodes=mv.scene(config)["nodes"].toArray();left=0;
        for(auto v:nodes)if(v.toObject()["type"]=="triangle"&&v.toObject()["direction"]=="left")++left;QCOMPARE(left,2);
        QCOMPARE(mv.clocks[0].elapsed,10.);QCOMPARE(mv.clocks[1].elapsed,25.);
    }

    void dmeBackgroundInheritance() {
        Configuration config;QVERIFY(!config.dmeBackgroundCustom("cube"));QCOMPARE(config.dmeBackground("cube"),-1);
        config.setDmeBackground("global",2);QCOMPARE(config.dmeBackground("cube"),2);QCOMPARE(config.dmeBackground("sony_1101"),2);
        config.setDmeBackground("cube",3);config.setDmeBackground("global",0);QCOMPARE(config.dmeBackground("cube"),3);QCOMPARE(config.dmeBackground("sony_1101"),0);
        config.setDmeBackgroundScope("cube",false);QCOMPARE(config.dmeBackground("cube"),0);
        config.setDmeBackgroundScope("cube",true);QCOMPARE(config.dmeBackground("cube"),3);
        config.setDmeBackgroundScope("cube",true,true);QCOMPARE(config.dmeBackground("cube"),0);
        QVERIFY(config.setDmeBackgroundImage("global","/media/brand.png"));QCOMPARE(config.dmeBackgroundImage("sony_1101"),QString("/media/brand.png"));QCOMPARE(config.dmeBackground("cube"),0);
        Configuration loaded;loaded.fromJson(config.toJson());QCOMPARE(loaded.dmeBackgrounds(),config.dmeBackgrounds());QCOMPARE(loaded.dmeBackgroundScopes(),config.dmeBackgroundScopes());
        auto legacy=config.toJson();auto transitions=legacy["transitions"].toObject();transitions.remove("dmeBackgroundScopes");transitions.insert("dmeBackgrounds",QJsonObject{{"cube",2},{"move",-1}});legacy["transitions"]=transitions;loaded.fromJson(legacy);
        QVERIFY(loaded.dmeBackgroundCustom("cube"));QVERIFY(!loaded.dmeBackgroundCustom("move"));QCOMPARE(loaded.dmeBackground("cube"),2);
        SetupWorkspace workspace(&config);auto* global=workspace.findChild<QComboBox*>("dmeBackground_global");QVERIFY(global);QCOMPARE(global->currentData().toInt(),-3);
    }

    void dmeBackgroundPreparation() {
        QTemporaryDir dir; Configuration config;
        config.setConfigFilePath(dir.filePath("dme.json"));
        config.setDmeBackground("cube",1002);
        SetupWorkspace workspace(&config);
        auto* cube = workspace.findChild<QComboBox*>("dmeBackground_cube"); QVERIFY(cube);
        QCOMPARE(cube->currentData().toInt(),1002);
        QCOMPARE(cube->findData(11),-1); QCOMPARE(cube->findData(23),-1);
        auto* turn = workspace.findChild<QComboBox*>("dmeBackground_page_curl"); QVERIFY(turn);
        QCOMPARE(turn->currentData().toInt(),-2);
        turn->setCurrentIndex(turn->findData(6));
        QVERIFY(workspace.isDirty()); QCOMPARE(config.dmeBackground("page_curl"),-1);
        auto* table = workspace.findChild<QTableWidget*>("sourceMatrix");
        table->item(6,2)->setText("Blue generator");
        QVERIFY(turn->currentText().contains("Blue generator"));
        Configuration draft; QVERIFY(workspace.applyTo(&draft));
        QCOMPARE(draft.dmeBackground("page_curl"),6);
        QCOMPARE(draft.dmeBackground("cube"),1002);
        draft.setConfigFilePath(config.configFilePath()); QVERIFY(draft.save());
        Configuration loaded; loaded.setConfigFilePath(config.configFilePath()); QVERIFY(loaded.load());
        QCOMPARE(loaded.dmeBackgrounds(),draft.dmeBackgrounds());
        workspace.reload(); QCOMPARE(turn->currentData().toInt(),-2);
        QCOMPARE(cube->currentData().toInt(),1002); QVERIFY(!workspace.isDirty());
        auto* nav = workspace.findChild<QListWidget*>("setupNavigation"); QVERIFY(nav);
        const auto sections = nav->findItems("DME backgrounds", Qt::MatchExactly);
        QCOMPARE(sections.size(),1);
        nav->setCurrentItem(sections.first());
        workspace.resize(1200,800); workspace.show(); QTest::qWait(40);
        QVERIFY(cube->isVisible());
        if (!qEnvironmentVariable("KAVTOR_DME_SETTINGS_SCREENSHOT").isEmpty()) {
            QVERIFY(workspace.grab().save(qEnvironmentVariable("KAVTOR_DME_SETTINGS_SCREENSHOT")));
        }
    }

    void sonyTransitionNamespaces() {
        QCOMPARE(supportedSonyWipes(true).size(),18);
        QCOMPARE(supportedSonyWipes(false).size(),10);
        for(int code:supportedSonyWipes(true)) {
            WipePattern pattern;bool reverse=false;
            QVERIFY(lookupWipeBySony(code,&pattern,&reverse));
            QVERIFY(!pattern.id.isEmpty());
        }
        WipePattern pattern;bool reverse=false;
        QVERIFY(lookupWipeBySony(22,&pattern,&reverse));
        QCOMPARE(pattern.id,QString("smil_sonyWipe_cross"));
        QVERIFY(lookupWipeBySony(13,&pattern,&reverse));
        QVERIFY(!supportedSonyWipes(true).contains(13));QCOMPARE(supportedSonyWipes(true,true).size(),41);
        QVERIFY(!lookupWipeBySony(1001,&pattern,&reverse));
        const QStringList dirs={"left","right","top","bottom"};
        for(int base:{1000,2600})for(int i=1;i<=4;++i){
            QString effect,dir;QVERIFY(lookupDmeBySony(base+i,&effect,&dir));
            QCOMPARE(effect,QString(base==1000?"slide":"push"));QCOMPARE(dir,dirs[i-1]);
        }
        QVERIFY(!lookupDmeBySony(22,nullptr,nullptr));
        QVERIFY(!lookupDmeBySony(1005,nullptr,nullptr));
        QCOMPARE(supportedSonyDmes(true).size(),44);
        QCOMPARE(supportedSonyDmes(true,true).size(),54);
        QCOMPARE(supportedSonyDmes(true,true,true).size(),67);QCOMPARE(supportedSonyDmes(true,true,true,true).size(),71);QCOMPARE(supportedSonyDmes(true,true,true,true,true).size(),72);QCOMPARE(pendingSonyDmes(true,true,true,true).size(),197);QCOMPARE(pendingSonyDmes(true,true,true).size(),198);QCOMPARE(pendingSonyDmes(true,true).size(),202);
        QVERIFY(!lookupDmeBySony(1051,nullptr,nullptr,true,true));QVERIFY(lookupDmeBySony(1051,nullptr,nullptr,true,true,true));
        QCOMPARE(pendingSonyDmes(true).size(),215);
        for(int code:QList<int>{1045,1046,1047,1048,1101,1102,1103,1104,1121,1122}){QVERIFY(!lookupDmeBySony(code,nullptr,nullptr,true));QVERIFY(lookupDmeBySony(code,nullptr,nullptr,true,true));}
        for(int code:supportedSonyDmes(true)){QString e,d;QVERIFY(lookupDmeBySony(code,&e,&d,true));QCOMPARE(e,QString("sony"));}
    }

    void staticDmeBackgroundPreparation() {
        QTemporaryDir dir;Configuration config;config.setConfigFilePath(dir.filePath("settings.json"));
        const QString path=dir.filePath("Backdrop image.png");
        QImage image(200,100,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(path));
        QVERIFY(config.setDmeBackgroundImage("cube",path));QCOMPARE(config.dmeBackground("cube"),-3);
        QVERIFY(config.save());Configuration loaded;loaded.setConfigFilePath(config.configFilePath());QVERIFY(loaded.load());
        QCOMPARE(loaded.dmeBackgroundImage("cube"),path);
        QCOMPARE(staticDmeImageProducer(path),"\""+path+"\" SCALE_MODE FILL");
        QVERIFY(staticDmeImageProducer("frame.png\nCLEAR 1").isEmpty());
        QVERIFY(staticDmeImageProducer("video.mp4").isEmpty());
        QVERIFY(staticDmeImageProducer("https://example.test/frame.png").isEmpty());
        QVERIFY(!loaded.setDmeBackgroundImage("unknown",path));
        SetupWorkspace workspace(&loaded);
        auto* choice=workspace.findChild<QComboBox*>("dmeBackground_cube");QVERIFY(choice);
        QCOMPARE(choice->currentData().toInt(),-3);
        auto* field=workspace.findChild<QLineEdit*>("dmeImage_cube");QVERIFY(field);QCOMPARE(field->text(),path);
        field->setText(dir.filePath("Other image.png"));
        QVERIFY(workspace.isDirty());QCOMPARE(loaded.dmeBackgroundImage("cube"),path);
        Configuration draft;QVERIFY(workspace.applyTo(&draft));QCOMPARE(draft.dmeBackgroundImage("cube"),field->text());
        field->clear();QVERIFY(!workspace.validationError().isEmpty());QVERIFY(!workspace.applyTo(&draft));
        workspace.reload();QCOMPARE(field->text(),path);QVERIFY(!workspace.isDirty());
        choice->setCurrentIndex(choice->findData(-1));QVERIFY(workspace.applyTo(&draft));QCOMPARE(draft.dmeBackground("cube"),-1);
    }

    void superSourcePerspectiveEditor() {
        Configuration config;SuperSourceLayout layout;layout.id="perspective";layout.name="Perspective";
        SuperSourceBox box;box.id="window";box.input=0;box.rect={.1,.1,.4,.4};layout.boxes={box};QVERIFY(config.setSuperSources({layout}));
        const auto identity=box.corners;QCOMPARE(superSourcePerspective(box),identity);
        SuperSourceEditor editor(&config);editor.selectBoxes({0});
        auto* corner=editor.findChild<QDoubleSpinBox*>("superSourceCorner0");QVERIFY(corner);corner->setValue(20);
        const auto edited=editor.layouts()[0].boxes[0];QCOMPARE(edited.corners[0].x(),.2);QCOMPARE(config.superSources()[0].boxes[0].corners,identity);
        QVERIFY(validSuperSourceCorners(edited.corners));
        auto encoded=superSourcesJson(editor.layouts());QList<SuperSourceLayout> decoded;QVERIFY(parseSuperSources(encoded,&decoded));QCOMPARE(decoded[0].boxes[0].corners,edited.corners);
        auto commands=superSourceCommands(editor.layouts()[0],7,16./9,[](int){return "route://1-1";});
        QVERIFY(std::any_of(commands.begin(),commands.end(),[](const QString& c){return c.startsWith("MIXER 7-1 PERSPECTIVE ");}));
        auto scene=moveSuperSourceScene(editor.layouts()[0],16./9,[](int){return "input:0";});
        QCOMPARE(scene[1].perspective,superSourcePerspective(edited));
        auto invalid=edited.corners;invalid[0]={2,2};QVERIFY(!validSuperSourceCorners(invalid));
        editor.findChild<QPushButton*>("superSourcePerspectiveReset")->click();QCOMPARE(editor.layouts()[0].boxes[0].corners,identity);
    }
    void enhancedSonyCatalogue() {
        QCOMPARE(supportedSonyWipes(true,true).size(),41);
        QCOMPARE(supportedSonyWipes(true,true,true).size(),53);
        QCOMPARE(supportedSonyWipes(true,true,true,true).size(),65);
        QCOMPARE(supportedSonyWipes(true,true,true,true,true).size(),83);
        QCOMPARE(pendingSonyWipes().size(),33);
        for(int code:pendingSonyWipes()){QVERIFY(!supportedSonyWipes(true,true,true,true,true).contains(code));QVERIFY(!wipePatternById(QString("smil_sonyWipe_pattern%1").arg(code)).implemented);}
        for(int code:supportedSonyWipes(true,true,true,true)){WipePattern p;bool reverse;QVERIFY(lookupWipeBySony(code,&p,&reverse));}
        for(int code:supportedSonyWipes(true,true,true)){WipePattern p;bool reverse;QVERIFY(lookupWipeBySony(code,&p,&reverse));}
        for(int code:supportedSonyWipes(true,true)){WipePattern p;bool reverse;QVERIFY(lookupWipeBySony(code,&p,&reverse));}
        Configuration config;config.setWipeGeometry(3,25);QCOMPARE(config.wipeVertices(),3);QCOMPARE(config.wipeRounding(),25);
        Configuration copied;copied.fromJson(config.toJson());QCOMPARE(copied.wipeVertices(),3);
        copied.setWipeGeometry(2,0);QCOMPARE(copied.wipeVertices(),3);
    }

    void sourceAwareMovePlan() {
        MoveElement camera;camera.identity="input:0";camera.volume=1;
        SuperSourceLayout layout;layout.id="two-up";layout.background="transparent";
        SuperSourceBox box;box.id="left";box.input=0;box.rect={0,0,.5,1};box.crop={.1,.2,.6,.7};box.audio=true;
        auto other=box;other.id="right";other.input=1;other.rect={.5,0,.5,1};other.audio=false;layout.boxes={box,other};
        auto resolve=[](int id){return id<0?QString():QString("input:%1").arg(id);};
        const auto scene=moveSuperSourceScene(layout,16./9,resolve);
        const auto plan=planMove({camera},scene);QCOMPARE(plan.size(),2);QVERIFY(plan[0].matched);QVERIFY(!plan[1].matched);
        QCOMPARE(sampleMove(plan,0).size(),1);auto end=sampleMove(plan,1);QCOMPARE(end.size(),2);QCOMPARE(end[0].crop,box.crop);QCOMPARE(end[0].clip,box.rect);
        auto middle=sampleMove(plan,.5);QCOMPARE(middle[1].opacity,.5);QCOMPARE(middle[0].volume,1.);
        QCOMPARE(sampleMove(plan,0)[0].clip,camera.clip); // Reversible after sampling midpoint.
        auto repeated=scene;repeated[1].identity=repeated[0].identity;
        auto ambiguous=planMove({camera},repeated);QCOMPARE(ambiguous.size(),3);for(auto& t:ambiguous)QVERIFY(!t.matched);
        auto stable=planMove(repeated,repeated);QCOMPARE(stable.size(),2);for(auto& t:stable)QVERIFY(t.matched);
        auto changed=moveSuperSourceScene(layout,16./9,resolve,{{"left",2}});QCOMPARE(changed[0].identity,QString("input:2"));
        QCOMPARE(scene[0].identity,QString("input:0")); // Frozen scenes never modify layouts or live bindings.
        auto ordered=scene;ordered[0].order=3;auto z=planMove(scene,ordered);QCOMPARE(sampleMove(z,.5)[0].order,2.);
        layout.background="#000000";auto background=moveSuperSourceScene(layout,16./9,resolve);QCOMPARE(background[0].order,-1.);
    }

    void superSourceRoundTripAndEditor() {
        QTemporaryDir dir;Configuration config;config.setConfigFilePath(dir.filePath("ss.json"));
        SuperSourceLayout layout;layout.id="two-up";layout.name="Two-up";
        SuperSourceBox a;a.id="left";a.name="Left";a.input=0;a.rect={0,0,.5,1};a.audio=true;
        SuperSourceBox b=a;b.id="right";b.name="Right";b.input=1;b.rect={.5,0,.5,1};b.audio=false;b.zoom=1.3;b.centerX=.65;
        layout.boxes={a,b};QVERIFY(config.setSuperSources({layout}));
        Source src=*config.sourceById(6);src.type=SourceType::SuperSource;src.argument=layout.id;src.enabled=true;config.setSource(src);
        QVERIFY(config.superSourceGraphError().isEmpty());QVERIFY(config.save());
        Configuration loaded;loaded.setConfigFilePath(config.configFilePath());QVERIFY(loaded.load());
        QCOMPARE(superSourcesJson(loaded.superSources()),superSourcesJson(config.superSources()));
        SetupWorkspace workspace(&config);workspace.resize(1400,1000);workspace.show();
        auto table=workspace.findChild<QTableWidget*>("sourceMatrix");QVERIFY(table);
        QCOMPARE(qobject_cast<QLineEdit*>(table->cellWidget(6,5))->text(),QString("Two-up"));
        auto nav=workspace.findChild<QListWidget*>("setupNavigation");QVERIFY(nav);for(int i=0;i<nav->count();++i)if(nav->item(i)->text()=="SuperSources")nav->setCurrentRow(i);QTest::qWait(30);
        auto editor=workspace.findChild<SuperSourceEditor*>("superSourceEditor");QVERIFY(editor);QCOMPARE(editor->layouts().size(),1);
        auto layers=editor->findChild<QListWidget*>("superSourceLayers");layers->setCurrentRow(0);
        auto fields=editor->findChildren<QDoubleSpinBox*>();QVERIFY(fields.size()>=11);
        editor->findChild<QDoubleSpinBox*>("superSourceGeometry0")->setValue(12);QCOMPARE(editor->layouts()[0].boxes[0].rect.x(),.12);
        QCOMPARE(config.superSources()[0].boxes[0].rect.x(),0.); // Draft only.
        QCOMPARE(editor->layouts()[0].boxes[0].id,QString("left"));
        QVERIFY(workspace.validationError().isEmpty());QVERIFY(workspace.applyTo(&config));
        QCOMPARE(config.superSources()[0].boxes[0].rect.x(),.12);
        QCoreApplication::processEvents();if(!qEnvironmentVariable("KAVTOR_SUPERSOURCE_SCREENSHOT").isEmpty())editor->grab().save(qEnvironmentVariable("KAVTOR_SUPERSOURCE_SCREENSHOT"));
    }

    void superSourceArrangementAndClipboard() {
        Configuration config;SuperSourceLayout l;l.id="grid";l.name="Grid";
        for(int i=0;i<3;++i){SuperSourceBox b;b.id=QString("b%1").arg(i);b.name=b.id;b.input=i;b.keyButton=i;b.rect={i*.3,.1,.2,.2};b.audio=i==0;l.boxes.append(b);}
        config.setSuperSources({l});SuperSourceEditor editor(&config);editor.selectBoxes({0,1,2});editor.alignSelection("bottom",true);
        for(auto& b:editor.layouts()[0].boxes)QVERIFY(qAbs(b.rect.bottom()-1)<1e-8);
        editor.undo();QCOMPARE(editor.layouts()[0].boxes[0].rect.top(),.1);
        editor.redo();QVERIFY(qAbs(editor.layouts()[0].boxes[0].rect.bottom()-1)<1e-8);
        editor.selectBoxes({0,1,2});editor.distributeSelection(true);QCOMPARE(editor.layouts()[0].boxes[1].rect.x(),.3);
        editor.arrangeGrid(2,0);QCOMPARE(editor.layouts()[0].boxes[0].rect,QRectF(0,0,.5,.5));
        editor.selectBoxes({0});auto before=editor.layouts()[0].boxes[0].id;editor.copySelection();editor.pasteBoxes();
        QCOMPARE(editor.layouts()[0].boxes.size(),4);QVERIFY(editor.layouts()[0].boxes[3].id!=before);QVERIFY(!editor.layouts()[0].boxes[3].audio);QCOMPARE(editor.layouts()[0].boxes[3].keyButton,3);
        QVERIFY(validateSuperSources(editor.layouts()).isEmpty());
        auto layout=editor.layouts()[0];layout.boxes[1].keyButton=layout.boxes[0].keyButton;QVERIFY(!validateSuperSources({layout}).isEmpty());
    }

    void superSourceValidationAndGeometry() {
        SuperSourceLayout layout;layout.id="layout";layout.name="Layout";SuperSourceBox box;box.id="box";box.input=0;box.rect={.25,.25,.5,.5};box.zoom=2;layout.boxes={box};
        QCOMPARE(superSourceFill(box),QRectF(0,0,1,1));
        auto commands=superSourceCommands(layout,7,16./9,[](int id){return QString("route://%1-1").arg(id+1);});
        QVERIFY(commands.contains("MIXER 7-1 CLIP 0.250000 0.250000 0.500000 0.500000"));
        QVERIFY(commands.contains("MIXER 7-1 VOLUME 0"));
        auto malformed=superSourcesJson({layout});auto l=malformed[0].toObject();auto boxes=l.value("boxes").toArray();auto boxJson=boxes[0].toObject();boxJson.insert("zoom","invalid");boxes[0]=boxJson;l.insert("boxes",boxes);malformed[0]=l;QList<SuperSourceLayout> parsed;QVERIFY(!parseSuperSources(malformed,&parsed));
        auto exportRoot=qEnvironmentVariable("KAVTOR_SUPERSOURCE_FIXTURE");
        if(!exportRoot.isEmpty()){
            QDir().mkpath(exportRoot);QJsonObject plans;SuperSourceLayout split=layout;split.background="#000000";
            split.boxes[0].rect={0,0,.5,1};split.boxes[0].zoom=1;split.boxes[0].audio=true;
            auto green=split.boxes[0];green.id="green";green.input=1;green.rect={.5,0,.5,1};green.audio=false;split.boxes.append(green);
            auto route=[](int input){return QString("route://%1-1").arg(input+1);};
            auto append=[&](QString name,const SuperSourceLayout& l){QJsonArray commands;for(auto& c:superSourceCommands(l,3,16./9,route))commands.append(c);plans.insert(name,commands);};
            append("split",split);
            auto zoomed=split;zoomed.boxes.removeLast();zoomed.boxes[0].rect={0,0,1,1};zoomed.boxes[0].zoom=2;zoomed.boxes[0].centerX=.75;append("zoom",zoomed);
            auto pip=split;pip.boxes[0].rect={0,0,1,1};pip.boxes[1].rect={.6,.6,.3,.3};append("pip",pip);
            QImage logo(64,64,QImage::Format_ARGB32);logo.fill(QColor(0,0,255,128));logo.save(exportRoot+"/logo.png");
            SuperSourceBox image;image.id="logo";image.kind="image";image.image=exportRoot+"/logo.png";image.imageAspect=1;image.cover=false;image.rect={.35,.35,.3,.3};pip.boxes.append(image);append("image",pip);auto transparent=pip;transparent.background="transparent";transparent.boxes={image};append("transparent",transparent);
            QFile file(exportRoot+"/plans.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(plans).toJson());
        }
        layout.boxes[0].rect={0,0,2,1};QVERIFY(!validateSuperSources({layout}).isEmpty());
        layout.boxes[0]=box;layout.boxes.append(box);QVERIFY(!validateSuperSources({layout}).isEmpty());
        Configuration config;layout.boxes={box};layout.boxes[0].input=6;QVERIFY(config.setSuperSources({layout}));
        Source src=*config.sourceById(6);src.type=SourceType::SuperSource;src.argument="layout";src.enabled=true;config.setSource(src);QVERIFY(!config.superSourceGraphError().isEmpty());
    }

    void standardColorBarsSelection() {
        QTemporaryDir dir; Configuration config;
        config.setConfigFilePath(dir.filePath("bars.json"));
        auto* input = config.sourceById(0); QVERIFY(input);
        input->type = SourceType::ColorBars; input->argument.clear(); input->enabled = true;
        SetupWorkspace workspace(&config);
        auto* table = workspace.findChild<QTableWidget*>("sourceMatrix"); QVERIFY(table);
        auto* argument = qobject_cast<QLineEdit*>(table->cellWidget(0,5)); QVERIFY(argument);
        auto* selector = qobject_cast<QPushButton*>(table->cellWidget(0,6)); QVERIFY(selector);
        QVERIFY(argument->isReadOnly()); QVERIFY(selector->isEnabled());
        QVERIFY(selector->menu()); QCOMPARE(selector->menu()->actions().size(), 4);
        QVERIFY(selector->text().contains("75%"));
        selector->menu()->actions().at(3)->trigger();
        QCOMPARE(argument->text(), QStringLiteral("SMPTEHD"));
        Configuration draft; QVERIFY(workspace.applyTo(&draft));
        draft.setConfigFilePath(config.configFilePath()); QVERIFY(draft.save());
        Configuration loaded; loaded.setConfigFilePath(config.configFilePath()); QVERIFY(loaded.load());
        QCOMPARE(loaded.sourceById(0)->producerCommand(), QStringLiteral("COLORBARS SMPTEHD"));
        auto* type = qobject_cast<QComboBox*>(table->cellWidget(0,3)); QVERIFY(type);
        type->setCurrentIndex(2); // Clip editing must work after leaving Bars.
        QVERIFY(!argument->isReadOnly()); QVERIFY(!selector->menu());
    }

    void matteSourceAndPersistence() {
        Source source; source.type = SourceType::Matte; source.argument = " #21aBcD ";
        QCOMPARE(source.producerCommand(), QStringLiteral("#21ABCD"));
        QVERIFY(source.isAssigned());
        for (const QString& invalid : {QString(), QString("#123"), QString("#FF123456"),
                QString("red"), QString("#12345Z"), QString("#123456\nCLEAR 1")}) {
            source.argument = invalid;
            QVERIFY2(source.producerCommand().isEmpty(), qPrintable(invalid));
            QVERIFY(!source.isAssigned());
        }
        SourceType type;
        QVERIFY(sourceTypeFromString("MATTE", &type)); QCOMPARE(type, SourceType::Matte);
        QVERIFY(sourceTypeFromString("color", &type)); QCOMPARE(type, SourceType::ColorBars);
        QTemporaryDir dir; Configuration config; config.setConfigFilePath(dir.filePath("matte.json"));
        auto* input = config.sourceById(0); QVERIFY(input);
        input->type = SourceType::Matte; input->argument = "#21ABCD"; input->enabled = true;
        config.setDipColor("#21aBcD");
        QCOMPARE(config.dipColor(), QStringLiteral("#21ABCD"));
        config.setDipColor("#NOTRGB");
        QCOMPARE(config.dipColor(), QStringLiteral("#21ABCD"));
        QVERIFY(config.save());
        Configuration loaded; loaded.setConfigFilePath(config.configFilePath()); QVERIFY(loaded.load());
        QCOMPARE(loaded.dipColor(), QStringLiteral("#21ABCD"));
        QCOMPARE(loaded.sourceById(0)->type, SourceType::Matte);
        QCOMPARE(loaded.sourceById(0)->producerCommand(), QStringLiteral("#21ABCD"));
        SetupWorkspace workspace(&loaded);
        auto* table = workspace.findChild<QTableWidget*>("sourceMatrix"); QVERIFY(table);
        auto* combo = qobject_cast<QComboBox*>(table->cellWidget(0,3)); QVERIFY(combo);
        QCOMPARE(combo->currentText(), QStringLiteral("Matte"));
        QVERIFY(table->cellWidget(0,6)->isEnabled());
        auto* argument = qobject_cast<QLineEdit*>(table->cellWidget(0,5)); QVERIFY(argument);
        argument->setText("#NOTRGB");
        QVERIFY(workspace.validationError().contains("matte colour"));
        Configuration prepared;
        QVERIFY(!workspace.applyTo(&prepared));
        argument->setText("#345678");
        QVERIFY(workspace.applyTo(&prepared));
        QCOMPARE(prepared.sourceById(0)->producerCommand(), QStringLiteral("#345678"));
    }

    void emptyAndUnsafeProducersAreUnassigned() {
        for (SourceType type : {SourceType::Html, SourceType::Ffmpeg, SourceType::File, SourceType::Still}) {
            Source source; source.type = type; source.argument = "   ";
            QVERIFY(source.producerCommand().isEmpty()); QVERIFY(!source.isAssigned());
            source.argument = "clip\nCLEAR 1";
            QVERIFY(source.producerCommand().isEmpty());
        }
        Source stream; stream.type = SourceType::Ffmpeg; stream.argument = "ffmpeg://";
        QVERIFY(!stream.isAssigned());
    }

    void video4LinuxInput() {
        Source source; source.type = SourceType::V4l2; source.argument = " /dev/video0 ";
        QVERIFY(source.isAssigned()); QVERIFY(!source.isClip());
        QVERIFY(source.producerCommand().startsWith("\"v4l2:///dev/video0\" SEEKABLE 0 VF "));
        source.argument = "v4l2:///dev/v4l/by-id/usb-Camera-video-index0";
        QVERIFY(source.isAssigned());
        source.argument = "/dev/v4l/by-path/pci-0000:00:14.0-usb-0:2:1.0-video-index0";
        QVERIFY(source.isAssigned());
        for (const QString& invalid : {QString(), QString("/tmp/video0"), QString("/dev/video0 SEEK 0"),
                                      QString("/dev/v4l/by-id/../../passwd"), QString("/dev/v4l/by-id/.."),
                                      QString("/dev/v4l/by-path/."), QString("/dev/video0\nCLEAR 1")}) {
            source.argument = invalid; QVERIFY(!source.isAssigned());
        }
        SourceType type; QVERIFY(sourceTypeFromString("V4L2", &type)); QCOMPARE(type, SourceType::V4l2);
        QTemporaryDir dir; Configuration config; config.setConfigFilePath(dir.filePath("capture.json"));
        auto* input = config.sourceById(0); QVERIFY(input);
        input->type = SourceType::V4l2; input->argument = "/dev/video2"; input->enabled = true;
        QVERIFY(config.save()); Configuration loaded; loaded.setConfigFilePath(config.configFilePath()); QVERIFY(loaded.load());
        QCOMPARE(loaded.sourceById(0)->type, SourceType::V4l2);
        QCOMPARE(loaded.sourceById(0)->argument, QString("/dev/video2"));
        SetupWorkspace workspace(&loaded); auto* table = workspace.findChild<QTableWidget*>("sourceMatrix"); QVERIFY(table);
        auto* selector = qobject_cast<QComboBox*>(table->cellWidget(0, 3)); QVERIFY(selector);
        QCOMPARE(selector->currentText(), QString("V4L2"));
        auto* descriptor = qobject_cast<QLineEdit*>(table->cellWidget(0, 5)); QVERIFY(descriptor);
        descriptor->setText("video2"); QVERIFY(workspace.validationError().contains("/dev/videoN"));
        descriptor->setText("/dev/video3"); QVERIFY(workspace.validationError().isEmpty());
        Configuration prepared; QVERIFY(workspace.applyTo(&prepared));
        QCOMPARE(prepared.sourceById(0)->argument, QString("/dev/video3"));
        QCOMPARE(loaded.sourceById(0)->argument, QString("/dev/video2"));
    }

    void keyProcessingDraft() {
        Configuration config;
        SetupWorkspace workspace(&config);
        auto* edit=workspace.findChild<QPushButton*>("key-processing-0");QVERIFY(edit);
        QTimer::singleShot(0,&workspace,[] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);
            dialog->findChild<QComboBox*>("key-mode")->setCurrentIndex(1);
            dialog->findChild<QCheckBox*>("key-mask")->setChecked(true);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });edit->click();
        QVERIFY(workspace.isDirty());
        QCOMPARE(config.keyProcessing(0,0).mode,QStringLiteral("linear"));
        Configuration prepared;QVERIFY(workspace.applyTo(&prepared));
        QCOMPARE(prepared.keyProcessing(0,0).mode,QStringLiteral("chroma"));
        QVERIFY(prepared.keyProcessing(0,0).mask);
        workspace.reload();QVERIFY(!workspace.isDirty());
        QTimer::singleShot(0,&workspace,[] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());QVERIFY(dialog);
            dialog->findChild<QComboBox*>("key-mode")->setCurrentIndex(1);
            dialog->reject();
        });edit->click();
        QVERIFY(!workspace.isDirty());
        QVERIFY(workspace.applyTo(&prepared));
        QCOMPARE(prepared.keyProcessing(0,0).mode,QStringLiteral("linear"));
    }
    void outputDestinationDraft() {
        QTemporaryDir dir;Configuration config;config.setConfigFilePath(dir.filePath("settings.json"));
        OutputDestination d;d.id=0;d.name="AUX monitor";d.enabled=true;d.aux=1;d.source=1200;QVERIFY(config.setDestinations({d}));
        SetupWorkspace workspace(&config);workspace.resize(1400,900);workspace.show();
        auto* table=workspace.findChild<QTableWidget*>("destinationTable");QVERIFY(table);QCOMPARE(table->rowCount(),1);
        table->item(0,1)->setText("Multiview AUX");QVERIFY(workspace.isDirty());
        QVERIFY(workspace.validationError().isEmpty());QVERIFY(workspace.applyTo(&config));QCOMPARE(config.destinations()[0].name,QString("Multiview AUX"));
        for(auto* list:workspace.findChildren<QListWidget*>())for(int i=0;i<list->count();++i)if(list->item(i)->text()==QString("Outputs"))list->setCurrentRow(i);
        QApplication::processEvents();
        if(qEnvironmentVariableIsSet("KAVTOR_SCREENSHOT"))workspace.grab().save(qEnvironmentVariable("KAVTOR_SCREENSHOT"));
    }

    void takeStartedDuringConfirmationBlocksApplication() {
        QTemporaryDir dir;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server] {
            auto* peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, peer, [peer] {
                while (peer->canReadLine()) {
                    peer->readLine();
                    peer->write("202 OK\r\n");
                }
                peer->flush();
            });
        });
        Configuration config;
        config.setConfigFilePath(dir.filePath("modal.json"));
        config.setCasparHost("127.0.0.1");
        config.setCasparPort(server.serverPort());
        AmcpClient client;
        client.setReconnectIntervalMs(0);
        SwitcherEngine engine(&config, &client);
        MainWindow window(&engine, nullptr);
        window.show();
        engine.connectToCaspar();
        QTRY_VERIFY(engine.isConnected());
        QTRY_COMPARE(client.queuedCommandCount(), 0);
        QVERIFY(!engine.isPreparing());
        const QString original = config.sourceName(0);
        window.findChild<QTableWidget*>("sourceMatrix")->item(0, 2)->setText("Hold this preparation");
        window.findChild<QPushButton*>("saveApplyButton")->click();
        auto* apply = window.findChild<QPushButton*>("applyPrepared");
        QVERIFY(apply->isEnabled());
        bool wasBusy = false;
        QTimer::singleShot(0, &window, [&engine, &wasBusy] {
            engine.selectPreview(0); // Simulate a physical-panel command during the modal dialog.
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            wasBusy = engine.isBusy();
            if (dialog) dialog->button(QMessageBox::Yes)->click();
        });
        apply->click();
        QVERIFY(wasBusy);
        QCOMPARE(config.sourceName(0), original);
        QVERIFY(window.findChild<QPlainTextEdit*>("sessionDiagnostics")->toPlainText().contains("in progress"));
        QTRY_COMPARE(engine.previewSource(), 0);
    }
    void failedSaveDoesNotReplaceConfiguration() {
        QTemporaryDir dir;
        Configuration config;
        config.setConfigFilePath(dir.path()); // A directory cannot be replaced by a JSON file.
        SetupWorkspace workspace(&config);
        const auto before = config.toJson();
        workspace.findChild<QTableWidget*>("sourceMatrix")->item(0, 2)->setText("Must not leak");
        QSignalSpy changes(&config, &Configuration::configurationChanged);
        QVERIFY(!workspace.applyTo(&config));
        QCOMPARE(config.toJson(), before);
        QCOMPARE(changes.count(), 0);
        QVERIFY(workspace.isDirty());
    }
    void failedConfigurationLoadIsAtomic() {
        QTemporaryDir dir; Configuration config; config.setConfigFilePath(dir.filePath("invalid.json"));
        config.setCasparHost("original-server"); config.setDipColor("#102030");
        config.sourceById(0)->name = "Original input";
        const QJsonObject before = config.toJson();
        QSignalSpy changes(&config, &Configuration::configurationChanged);
        QJsonObject invalid = before;
        auto caspar = invalid.value("casparcg").toObject(); caspar["host"] = "must-not-leak"; invalid["casparcg"] = caspar;
        auto outputs = invalid.value("outputs").toObject();
        outputs["destinations"] = QJsonArray{QJsonObject{{"id", 0}, {"type", "invalid"}}}; invalid["outputs"] = outputs;
        QVERIFY(!config.fromJson(invalid)); QCOMPARE(config.toJson(), before); QCOMPARE(changes.count(), 0);
        QFile file(config.configFilePath()); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(invalid).toJson()); file.close();
        QVERIFY(!config.load()); QCOMPARE(config.toJson(), before); QCOMPARE(changes.count(), 0);

        invalid = before;
        auto sources = invalid.value("sources").toArray(); auto source = sources[0].toObject();
        source["name"] = "Must not survive a late failure"; sources[0] = source; invalid["sources"] = sources;
        auto transitions = invalid.value("transitions").toObject(); auto stingers = transitions.value("stingers").toArray();
        auto stinger = stingers[0].toObject(); stinger["media"] = "bad\"media"; stingers[0] = stinger;
        transitions["stingers"] = stingers; invalid["transitions"] = transitions;
        QVERIFY(!config.fromJson(invalid)); QCOMPARE(config.toJson(), before); QCOMPARE(changes.count(), 0);

        auto valid = before; caspar = valid.value("casparcg").toObject(); caspar["host"] = "new-server";
        valid["casparcg"] = caspar; QVERIFY(config.fromJson(valid)); QCOMPARE(changes.count(), 1);
        QCOMPARE(config.casparHost(), QString("new-server"));
    }

    void invalidMediaDoesNotPartiallyReplaceConfiguration() {
        QTemporaryDir dir;
        Configuration config;
        config.setConfigFilePath(dir.filePath("settings.json"));
        SetupWorkspace workspace(&config);
        const auto before = config.toJson();
        workspace.findChild<QTableWidget*>("sourceMatrix")->item(0, 2)->setText("Must not leak");
        workspace.findChild<QTableWidget*>("stingerLibrary")->item(0, 0)->setText("bad\"media");
        QVERIFY(!workspace.applyTo(&config));
        QCOMPARE(config.toJson(), before);
        QVERIFY(!QFile::exists(config.configFilePath()));
    }
    void validatesNdiNamesAndDeviceNumbers() {
        Configuration config;
        SetupWorkspace workspace(&config);
        auto* clean = workspace.findChild<QLineEdit*>("ndiCleanName");
        auto* program = workspace.findChild<QLineEdit*>("ndiProgramName");
        clean->setText(program->text());
        QVERIFY(workspace.validationError().contains("distinct"));
        clean->clear();
        QVERIFY(workspace.validationError().contains("needs a name"));
        workspace.reload();
        auto* inputs = workspace.findChild<QTableWidget*>("sourceMatrix");
        qobject_cast<QComboBox*>(inputs->cellWidget(0, 3))->setCurrentIndex(0);
        qobject_cast<QLineEdit*>(inputs->cellWidget(0, 5))->setText("not a device");
        QVERIFY(workspace.validationError().contains("DeckLink"));
        qobject_cast<QLineEdit*>(inputs->cellWidget(0, 5))->setText("1");
        QVERIFY(workspace.validationError().isEmpty());
        workspace.reload();
        QVERIFY(!workspace.isDirty());
        workspace.setInputState(0, "Prepared");
        QCOMPARE(inputs->item(0, 7)->text(), QString("Prepared"));
        QVERIFY(!workspace.isDirty());
    }
    void occupiedPanelPortPreservesExistingListenerAndSettings() {
        QTemporaryDir dir;
        Configuration config;
        config.setConfigFilePath(dir.filePath("settings.json"));
        AmcpClient client;
        SwitcherEngine engine(&config, &client);
        PanelProtocol panel(&engine);
        QVERIFY(panel.start(0));
        const int originalPort = panel.port();
        config.setPanelPort(originalPort);
        QTcpSocket peer;
        peer.connectToHost(QHostAddress::LocalHost, originalPort);
        QTRY_COMPARE(peer.state(), QAbstractSocket::ConnectedState);
        QTRY_VERIFY(peer.bytesAvailable() > 0);
        peer.readAll();
        QTcpServer occupied;
        QVERIFY(occupied.listen(QHostAddress::LocalHost));
        MainWindow window(&engine, &panel);
        const auto before = config.toJson();
        window.findChild<QSpinBox*>("panelPort")->setValue(occupied.serverPort());
        window.findChild<QPushButton*>("saveApplyButton")->click();
        window.findChild<QPushButton*>("applyPrepared")->click();
        QCOMPARE(config.toJson(), before);
        QCOMPARE(panel.port(), static_cast<quint16>(originalPort));
        QVERIFY(panel.isListening());
        QCOMPARE(peer.state(), QAbstractSocket::ConnectedState);
        peer.write("{\"cmd\":\"state\"}\n");
        peer.flush();
        QTRY_VERIFY(peer.bytesAvailable() > 0);
        QVERIFY(peer.readAll().contains("state"));
        QVERIFY(window.findChild<QPlainTextEdit*>("sessionDiagnostics")->toPlainText().contains("unavailable"));
    }
    void draftSaveDiscardAndValidation() {
        QTemporaryDir dir;
        Configuration config;
        config.setConfigFilePath(dir.filePath("setup.json"));
        SetupWorkspace workspace(&config);
        QVERIFY(!workspace.isDirty());
        auto* inputs = workspace.findChild<QTableWidget*>("sourceMatrix");
        QVERIFY(inputs);
        inputs->item(0, 2)->setText("Renamed input");
        QVERIFY(workspace.isDirty());
        QVERIFY(config.sourceName(0) != "Renamed input");
        workspace.reload();
        QVERIFY(!workspace.isDirty());
        QCOMPARE(inputs->item(0, 2)->text(), config.sourceName(0));
        inputs->item(0, 2)->setText("Prepared input");
        QVERIFY(workspace.applyTo(&config));
        QCOMPARE(config.sourceName(0), QString("Prepared input"));
        QVERIFY(QFile::exists(config.configFilePath()));
        auto* channel = qobject_cast<QSpinBox*>(inputs->cellWidget(0, 4));
        channel->setValue(config.programChannel());
        QVERIFY(!workspace.validationError().isEmpty());
        QVERIFY(!workspace.applyTo(&config));
        QCOMPARE(config.sourceById(0)->casparChannel, 1);
        workspace.reload();
        auto* stingers = workspace.findChild<QTableWidget*>("stingerLibrary");
        QCOMPARE(stingers->rowCount(), 10);
        qobject_cast<QSpinBox*>(stingers->cellWidget(0, 2))->setValue(100);
        qobject_cast<QSpinBox*>(stingers->cellWidget(0, 3))->setValue(50);
        QVERIFY(!workspace.validationError().isEmpty());
    }
    void managerHasNoLiveControlsAndSavesWithoutApplying() {
        QTemporaryDir dir;
        Configuration config;
        config.setConfigFilePath(dir.filePath("manager.json"));
        AmcpClient client;
        SwitcherEngine engine(&config, &client);
        MainWindow window(&engine, nullptr);
        window.show();
        auto* inputs = window.findChild<QTableWidget*>("sourceMatrix");
        const QString previous = config.sourceName(0);
        inputs->item(0, 2)->setText("Offline preparation");
        auto* save = window.findChild<QPushButton*>("saveApplyButton");
        QVERIFY(save);
        save->click();
        QCOMPARE(config.sourceName(0), previous);
        Configuration saved;
        saved.setConfigFilePath(config.configFilePath());
        QVERIFY(saved.load());
        QCOMPARE(saved.sourceName(0), QString("Offline preparation"));
        QCOMPARE(inputs->item(0, 7)->text(), QString("Pending apply"));
        QVERIFY(window.findChild<QLabel*>("configurationState")->text().contains("differs"));
        for (auto* button : window.findChildren<QPushButton*>()) {
            QVERIFY(button->text() != "CUT");
            QVERIFY(button->text() != "AUTO");
        }
        QTest::keyClick(&window, Qt::Key_Space);
        QTest::keyClick(&window, Qt::Key_1);
        QCOMPARE(engine.previewSource(), -1);
        QCOMPARE(engine.programSource(), -1);
        window.findChild<QPushButton*>("applyPrepared")->click();
        QCOMPARE(config.sourceName(0), QString("Offline preparation"));
        QCOMPARE(inputs->item(0, 7)->text(), QString("Offline"));
        QVERIFY(window.findChild<QLabel*>("configurationState")->text().contains("matches"));
        QVERIFY(!window.findChild<QPushButton*>("applyPrepared")->isEnabled());
        QFile style(":/styles/styles.css");
        QVERIFY(style.open(QIODevice::ReadOnly));
        qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
        window.findChild<QListWidget*>("setupNavigation")->setCurrentRow(1);
        QTest::qWait(50);
        QVERIFY(window.grab().save("/tmp/kavtor-manager-inputs.png"));
    }
};
QTEST_MAIN(TestManagement)
#include "test_management.moc"
