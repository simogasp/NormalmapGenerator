#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "graphicsscene.h"
#include "normalmapgenerator.h"
#include "specularmapgenerator.h"
#include "ssaogenerator.h"
#include "intensitymap.h"
#include "boxblur.h"
#include "aboutdialog.h"

#include <QMessageBox>
#include <QFileDialog>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QTreeView>
#include <QGraphicsPixmapItem>

#include <iostream>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //connect signals of GUI elements with slots of this class
    connectSignalSlots();

    //hide advanced settings and connect signals/slots to show them
    hideAdvancedSettings();

    //initialize graphicsview
    GraphicsScene *scene = new GraphicsScene();
    ui->graphicsView->setScene(scene);
    scene->setBackgroundBrush(QBrush(Qt::darkGray));
    ui->graphicsView->setDragMode(QGraphicsView::ScrollHandDrag);
    scene->addText("Start by dragging images here.");
    ui->graphicsView->setRenderHints(QPainter::HighQualityAntialiasing | QPainter::SmoothPixmapTransform);
    ui->graphicsView->setAcceptDrops(true);

    //initialize QImage objects to store the calculated maps
    input = QImage();
    channelIntensity = QImage();
    normalmap = QImage();
    specmap = QImage();
    displacementmap = QImage();
    ssaomap = QImage();

    //initialize calctimes
    lastCalctime_normal = 0;
    lastCalctime_specular = 0;
    lastCalctime_displace = 0;
    lastCalctime_ssao = 0;

    //initialize stopQueue flag
    stopQueue = false;

    //show default status message
    ui->statusBar->showMessage("Drag images into the empty preview window to load them.");

    //if the program was opened via "open with" by the OS, extract the image paths from the arguments
    //(args[0] is the name of the application)
    QStringList args = QCoreApplication::arguments();
    if(args.size() > 1) {
        QList<QUrl> imageUrls;

        for(int i = 1; i < args.size(); i++) {
            imageUrls.append(QUrl::fromLocalFile(args[i]));
        }

        loadMultipleDropped(imageUrls);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadSingleDropped(QUrl url) {
    if(load(url))
        addImageToQueue(url);
}

void MainWindow::loadMultipleDropped(QList<QUrl> urls) {
    //test if image formats are supported
    bool containedInvalidFormat = false;
    bool loadedFirstValidImage = false;

    for(int i = 0; i < urls.size(); i++) {
        QString suffix = QFileInfo(urls.at(i).fileName()).suffix().toLower();
        QString supported = "png jpg jpeg tiff ppm bmp xpm tga";

        if(supported.contains(suffix)) {
            //image format is supported, add to queue
            addImageToQueue(urls.at(i));
            //if it is the first valid image, load and preview it
            if(!loadedFirstValidImage) {
                load(urls.at(i));
                loadedFirstValidImage = true;
            }
        }
        else {
            containedInvalidFormat = true;
        }
    }

    if(containedInvalidFormat)
        QMessageBox::information(this, "Not All Images Loaded Into Queue",
                                 "Some images had unsupported formats and where not loaded into the queue!");
}

//load the image specified in the url
bool MainWindow::load(QUrl url) {
    if(!url.isValid()) {
        throw "[load] invalid url!";
        return false;
    }

    ui->statusBar->showMessage("loading Image: " + url.fileName());

    //load the image
    input = QImage(url.toLocalFile());

    QFileInfo file(url.toLocalFile());

    if(input.isNull()) {
        QString errorMessage("Image not loaded!");

        if(file.suffix().toLower() == "tga") {
            errorMessage.append("\nOnly uncompressed TGA files are supported.");
        }
        else {
            errorMessage.append("\nMost likely the image format is not supported.");
        }

        ui->statusBar->showMessage("Error: Image " + url.fileName() + " NOT loaded!", 5000);
        QMessageBox::information(this, "Error while loading image", errorMessage);
        return false;
    }

    //store the path the image was loaded from (for saving later)
    if(exportPath.isEmpty())
        exportPath = url.adjusted(QUrl::RemoveFilename);
    loadedImagePath = url;

    //enable ui buttons
    ui->pushButton_calcNormal->setEnabled(true);
    ui->pushButton_calcSpec->setEnabled(true);
    ui->pushButton_calcDisplace->setEnabled(true);
    ui->pushButton_calcSsao->setEnabled(true);
    ui->checkBox_displayChannelIntensity->setEnabled(true);
    ui->spinBox_normalmapSize->setEnabled(true);
    enableAutoupdate(true);
    
    //algorithm to find best settings for KeepLargeDetail
    int imageSize = std::max(input.width(), input.height());
    
    int largeDetailScale = -0.037 * imageSize + 100;
    ui->checkBox_keepLargeDetail->setChecked(true);
    
    if(imageSize < 300) {
        ui->checkBox_keepLargeDetail->setChecked(false);
    }
    else if(imageSize > 2300) {
        largeDetailScale = 20;
    }
    
    ui->spinBox_largeDetailScale->setValue(largeDetailScale);
    
    //switch active tab to input
    ui->tabWidget->setCurrentIndex(0);

    //clear all previously generated images
    channelIntensity = QImage();
    normalmap = QImage();
    specmap = QImage();
    displacementmap = QImage();
    ssaomap = QImage();

    //display single image channels if the option was already chosen
    if(ui->checkBox_displayChannelIntensity->isChecked())
        displayChannelIntensity();
    else
        preview(0);
    
    //image smaller than graphicsview: fitInView, then resetZoom (this way it is centered)
    //image larger than graphicsview: just fitInView
    fitInView();
    if(input.width() < ui->graphicsView->width() || input.height() < ui->graphicsView->height()) {
        resetZoom();
    }

    ui->statusBar->clearMessage();

    return true;
}

//load images using the file dialog
void MainWindow::loadUserFilePath() {
    QList<QUrl> urls = QFileDialog::getOpenFileUrls(this,
                                                     "Open Image File",
                                                     QDir::homePath(),
                                                     "Image Formats (*.png *.jpg *.jpeg *.tiff *.ppm *.bmp *.xpm *.tga)");
    loadMultipleDropped(urls);
}

void MainWindow::calcNormal() {
    if(input.isNull())
        return;

    //normalmap parameters
    double strength = ui->doubleSpinBox_strength->value();
    bool invert = ui->checkBox_invertHeight->isChecked();
    bool tileable = ui->checkBox_tileable->isChecked();

    //color channel mode
    IntensityMap::Mode mode = IntensityMap::AVERAGE;
    if(ui->comboBox_mode_normal->currentIndex() == 0)
        mode = IntensityMap::AVERAGE;
    else if(ui->comboBox_mode_normal->currentIndex() == 1)
        mode = IntensityMap::MAX;

    //color channels to use
    bool useRed = ui->checkBox_useRed_normal->isChecked();
    bool useGreen = ui->checkBox_useGreen_normal->isChecked();
    bool useBlue = ui->checkBox_useBlue_normal->isChecked();
    bool useAlpha = ui->checkBox_useAlpha_normal->isChecked();

    //kernel to use
    NormalmapGenerator::Kernel kernel = NormalmapGenerator::SOBEL;
    if(ui->comboBox_method->currentIndex() == 0)
        kernel = NormalmapGenerator::SOBEL;
    else if(ui->comboBox_method->currentIndex() == 1)
        kernel = NormalmapGenerator::PREWITT;
    
    //keep large detail settings
    bool keepLargeDetail = ui->checkBox_keepLargeDetail->isChecked();
    int largeDetailScale = ui->spinBox_largeDetailScale->value();
    double largeDetailHeight = ui->doubleSpinBox_largeDetailHeight->value();

    //scale input image if not 100%
    QImage inputScaled = input;
    int sizePercent = ui->spinBox_normalmapSize->value();
    if(sizePercent != 100) {
        int scaledWidth = calcPercentage(input.width(), sizePercent);
        int scaledHeight = calcPercentage(input.height(), sizePercent);

        inputScaled = input.scaled(scaledWidth, scaledHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    
    //setup generator and calculate map
    NormalmapGenerator normalmapGenerator(mode, useRed, useGreen, useBlue, useAlpha);
    normalmap = normalmapGenerator.calculateNormalmap(inputScaled, kernel, strength, invert, tileable, keepLargeDetail, largeDetailScale, largeDetailHeight);
    normalmapRawIntensity = normalmapGenerator.getIntensityMap().convertToQImage();
}

void MainWindow::calcSpec() {
    if(input.isNull())
        return;

    //color channel mode
    IntensityMap::Mode mode = IntensityMap::AVERAGE;
    if(ui->comboBox_mode_spec->currentIndex() == 0)
        mode = IntensityMap::AVERAGE;
    else if(ui->comboBox_mode_spec->currentIndex() == 1)
        mode = IntensityMap::MAX;

    //color channel multipliers to use
    double redMultiplier = ui->doubleSpinBox_spec_redMul->value();
    double greenMultiplier = ui->doubleSpinBox_spec_greenMul->value();
    double blueMultiplier = ui->doubleSpinBox_spec_blueMul->value();
    double alphaMultiplier = ui->doubleSpinBox_spec_alphaMul->value();
    double scale = ui->doubleSpinBox_spec_scale->value();
    double contrast = ui->doubleSpinBox_spec_contrast->value();

    //setup generator and calculate map
    SpecularmapGenerator specularmapGenerator(mode, redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier);
    specmap = specularmapGenerator.calculateSpecmap(input, scale, contrast);
}

//the displacement map is generated with the specularmapGenerator (similar controls and output needed)
void MainWindow::calcDisplace() {
    if(input.isNull())
        return;

    //color channel mode
    IntensityMap::Mode mode = IntensityMap::AVERAGE;
    if(ui->comboBox_mode_displace->currentIndex() == 0)
        mode = IntensityMap::AVERAGE;
    else if(ui->comboBox_mode_displace->currentIndex() == 1)
        mode = IntensityMap::MAX;

    //color channel multipliers to use
    double redMultiplier = ui->doubleSpinBox_displace_redMul->value();
    double greenMultiplier = ui->doubleSpinBox_displace_greenMul->value();
    double blueMultiplier = ui->doubleSpinBox_displace_blueMul->value();
    double alphaMultiplier = 0.0;
    double scale = ui->doubleSpinBox_displace_scale->value();
    double contrast = ui->doubleSpinBox_displace_contrast->value();

    //setup generator and calculate map
    SpecularmapGenerator specularmapGenerator(mode, redMultiplier, greenMultiplier, blueMultiplier, alphaMultiplier);
    displacementmap = specularmapGenerator.calculateSpecmap(input, scale, contrast);

    if(ui->checkBox_displace_blur->isChecked()) {
        int radius = ui->spinBox_displace_blurRadius->value();
        bool tileable = ui->checkBox_displace_blur_tileable->isChecked();

        IntensityMap inputMap(displacementmap, IntensityMap::AVERAGE);
        BoxBlur filter;
        IntensityMap outputMap = filter.calculate(inputMap, radius, tileable);
        displacementmap = outputMap.convertToQImage();
    }
}

void MainWindow::calcSsao() {
    if(input.isNull())
        return;

    //if no normalmap was created yet, calculate it
    if(normalmap.isNull()) {
        calcNormal();
    }

    //scale depthmap (can be smaller than normalmap because of KeepLargeDetail
    normalmapRawIntensity = normalmapRawIntensity.scaled(normalmap.width(), normalmap.height());
    float size = ui->doubleSpinBox_ssao_size->value();
    unsigned int samples = ui->spinBox_ssao_samples->value();
    unsigned int noiseTexSize = ui->spinBox_ssao_noiseTexSize->value();

    //setup generator and calculate map
    SsaoGenerator ssaoGenerator;
    ssaomap = ssaoGenerator.calculateSsaomap(normalmap, normalmapRawIntensity, size, samples, noiseTexSize);
}


void MainWindow::calcNormalAndPreview() {
    ui->statusBar->showMessage("calculating normalmap...");

    //timer for measuring calculation time
    QElapsedTimer timer;
    timer.start();

    //calculate map
    calcNormal();

    //display time it took to calculate the map
    this->lastCalctime_normal = timer.elapsed();
    displayCalcTime(lastCalctime_normal, "normalmap", 5000);

    //enable ui buttons
    ui->pushButton_save->setEnabled(true);

    //preview in normalmap tab
    preview(1);
}

void MainWindow::calcSpecAndPreview() {
    ui->statusBar->showMessage("calculating specularmap...");

    //timer for measuring calculation time
    QElapsedTimer timer;
    timer.start();

    //calculate map
    calcSpec();

    //display time it took to calculate the map
    this->lastCalctime_specular = timer.elapsed();
    displayCalcTime(lastCalctime_specular, "specularmap", 5000);

    //enable ui buttons
    ui->pushButton_save->setEnabled(true);

    //preview in specular map tab
    preview(2);
}

void MainWindow::calcDisplaceAndPreview() {
    ui->statusBar->showMessage("calculating displacementmap...");

    //timer for measuring calculation time
    QElapsedTimer timer;
    timer.start();

    //calculate map
    calcDisplace();

    //display time it took to calculate the map
    this->lastCalctime_displace = timer.elapsed();
    displayCalcTime(lastCalctime_displace, "displacementmap", 5000);

    //enable ui buttons
    ui->pushButton_save->setEnabled(true);

    //preview in displacement map tab
    preview(3);
}

void MainWindow::calcSsaoAndPreview() {
    ui->statusBar->showMessage("calculating ambient occlusion map...");

    //timer for measuring calculation time
    QElapsedTimer timer;
    timer.start();

    //calculate map
    calcSsao();

    //display time it took to calculate the map
    this->lastCalctime_ssao = timer.elapsed();
    displayCalcTime(lastCalctime_ssao, "ambient occlusion map", 5000);

    //enable ui buttons
    ui->pushButton_save->setEnabled(true);

    //preview in ambient occlusion map tab
    preview(4);
}

void MainWindow::processQueue() {
    if(ui->listWidget_queue->count() == 0)
        return;

    if(!exportPath.isValid()) {
        QMessageBox::information(this, "Invalid Export Path", "Export path is invalid!");
        return;
    }

    //enable stop button
    ui->pushButton_stopProcessingQueue->setEnabled(true);

    double percentageBase = 100.0 / ui->listWidget_queue->count();

    for(int i = 0; i < ui->listWidget_queue->count() && !stopQueue; i++)
    {
        QueueItem *item = (QueueItem*)(ui->listWidget_queue->item(i));

        //display status
        ui->statusBar->showMessage("Processing Queue Item: " + item->text());
        ui->progressBar_Queue->setValue((int)(percentageBase * (i + 1)));
        ui->listWidget_queue->item(i)->setSelected(true);

        //load image
        load(item->getUrl());

        //calculate maps
        if(ui->checkBox_queue_generateNormal->isChecked())
            calcNormal();
        if(ui->checkBox_queue_generateSpec->isChecked())
            calcSpec();
        if(ui->checkBox_queue_generateDisplace->isChecked())
            calcDisplace();

        //save maps
        QUrl exportUrl = QUrl::fromLocalFile(exportPath.toLocalFile() + "/" + item->text());
        std::cout << "[Queue] Image " << i + 1 << " exported: "
                  << exportUrl.toLocalFile().toStdString() << std::endl;
        save(exportUrl);

        //user interface should stay responsive
        QCoreApplication::processEvents();
    }

    //disable stop button
    ui->pushButton_stopProcessingQueue->setEnabled(false);
    stopQueue = false;

    //enable "Open Export Folder" gui button
    ui->pushButton_openExportFolder->setEnabled(true);
}

//tell the queue to stop processing
void MainWindow::stopProcessingQueue() {
    stopQueue = true;
}

//save maps using the file dialog
void MainWindow::saveUserFilePath() {
    QFileDialog::Options options(QFileDialog::DontConfirmOverwrite);
    QUrl url = QFileDialog::getSaveFileUrl(this, "Save as", loadedImagePath,
                                           "Image Formats (*.png *.jpg *.jpeg *.tiff *.ppm *.bmp *.xpm)",
                                           0, options);
    save(url);
}

void MainWindow::save(QUrl url) {
    //if saving process was aborted
    if(!url.isValid())
        return;

    QString path = url.toLocalFile();

    //if no file suffix was chosen, automatically use the PNG format
    QFileInfo file(path);
    if(!file.baseName().isEmpty() && file.suffix().isEmpty())
        path += ".png";

    QString suffix = file.suffix();
    //Qt can only read tga, saving is not supported
    if(suffix.toLower() == "tga")
        suffix = "png";

    //append a suffix to the map names (result: path/original_normal.png)
    QString name_normal = file.absolutePath() + "/" + file.baseName() + "_normal." + suffix;
    QString name_specular = file.absolutePath() + "/" + file.baseName() + "_spec." + suffix;
    QString name_displace = file.absolutePath() + "/" + file.baseName() + "_displace." + suffix;

    //check if maps where generated, if yes, check if it could be saved
    if(!normalmap.isNull() && ui->checkBox_queue_generateNormal->isChecked()) {
        if(!normalmap.save(name_normal))
            QMessageBox::information(this, "Error while saving Normalmap", "Normalmap not saved!");
        else
            ui->statusBar->showMessage("Normalmap saved as \"" + name_normal + "\"", 4000);
    }

    if(!specmap.isNull() && ui->checkBox_queue_generateSpec->isChecked()) {
        if(!specmap.save(name_specular))
            QMessageBox::information(this, "Error while saving Specularmap", "Specularmap not saved!");
        else
            ui->statusBar->showMessage("Specularmap saved as \"" + name_specular + "\"", 4000);
    }

    if(!displacementmap.isNull() && ui->checkBox_queue_generateDisplace->isChecked()) {
        if(!displacementmap.save(name_displace))
            QMessageBox::information(this, "Error while saving Displacementmap", "Displacementmap not saved!");
        else
            ui->statusBar->showMessage("Displacementmap saved as \"" + name_displace + "\"", 4000);
    }

    //store export path
    exportPath = url.adjusted(QUrl::RemoveFilename);
    //enable "Open Export Folder" gui button
    ui->pushButton_openExportFolder->setEnabled(true);
}

//change the path the queue exports the maps to
void MainWindow::changeOutputPathQueue() {
    QUrl startUrl = QDir::homePath();
    if(exportPath.isValid())
        startUrl = exportPath;

    exportPath = QFileDialog::getExistingDirectoryUrl(this,
                                                           "Choose Export Folder",
                                                           startUrl);
    std::cout << "export path changed to: " << exportPath.toLocalFile().toStdString() << std::endl;
}

//enable/disable custom output path button
void MainWindow::updateQueueExportOptions() {
    ui->pushButton_changeOutputPath_Queue->setEnabled(ui->radioButton_exportUserDefined->isChecked());
}

//overloaded version of preview that chooses the map to preview automatically
void MainWindow::preview() {
    preview(ui->tabWidget->currentIndex());
}

//preview the map in the selected tab
void MainWindow::preview(int tab) {
    ui->graphicsView->scene()->clear();

    switch(tab) {
    case 0:
    {
        //input
        if(ui->checkBox_displayChannelIntensity->isChecked() && !input.isNull()) {
            QGraphicsPixmapItem *pixmap = ui->graphicsView->scene()->addPixmap(QPixmap::fromImage(channelIntensity));
            pixmap->setTransformationMode(Qt::SmoothTransformation);
        }
        else {
            QGraphicsPixmapItem *pixmap = ui->graphicsView->scene()->addPixmap(QPixmap::fromImage(input));
            pixmap->setTransformationMode(Qt::SmoothTransformation);
        }
        break;
    }
    case 1:
    {
        //normal
        if(!input.isNull() && normalmap.isNull()) {
            //if an image was loaded and a normalmap was not yet generated and the image is not too large
            //automatically generate the normalmap
            calcNormalAndPreview();
        }
        QGraphicsPixmapItem *pixmap = ui->graphicsView->scene()->addPixmap(QPixmap::fromImage(normalmap));
        pixmap->setTransformationMode(Qt::SmoothTransformation);
        //display size of the image
        normalmapSizeChanged();
        break;
    }
    case 2:
    {
        //spec
        if(!input.isNull() && specmap.isNull()) {
            //if an image was loaded and a specmap was not yet generated and the image is not too large
            //automatically generate the specmap
            calcSpecAndPreview();
        }
        QGraphicsPixmapItem *pixmap = ui->graphicsView->scene()->addPixmap(QPixmap::fromImage(specmap));
        pixmap->setTransformationMode(Qt::SmoothTransformation);
        break;
    }
    case 3:
    {
        //displacement
        if(!input.isNull() && displacementmap.isNull()) {
            //if an image was loaded and a dispmap was not yet generated and the image is not too large
            //automatically generate the displacementmap
            calcDisplaceAndPreview();
        }
        QGraphicsPixmapItem *pixmap = ui->graphicsView->scene()->addPixmap(QPixmap::fromImage(displacementmap));
        pixmap->setTransformationMode(Qt::SmoothTransformation);
        break;
    }
    case 4:
    {
        //ambient occlusion
        if(!input.isNull() && ssaomap.isNull()) {
            //if an image was loaded and a ssaomap was not yet generated and the image is not too large
            //automatically generate the ambient occlusion map
            calcSsaoAndPreview();
        }
        QGraphicsPixmapItem *pixmap = ui->graphicsView->scene()->addPixmap(QPixmap::fromImage(ssaomap));
        pixmap->setTransformationMode(Qt::SmoothTransformation);
        break;
    }
    }
}

void MainWindow::zoomIn() {
    ui->graphicsView->scale(1.2, 1.2);
}

void MainWindow::zoomOut() {
    ui->graphicsView->scale(0.8, 0.8);
}

//resets zoom to 1:1
void MainWindow::resetZoom() {
    ui->graphicsView->resetTransform();
}

//fits the preview into the graphicsView
void MainWindow::fitInView() {
   ui->graphicsView->scene()->setSceneRect(QRectF(0, 0, input.width(), input.height()));
   ui->graphicsView->setSceneRect(ui->graphicsView->scene()->sceneRect());
   ui->graphicsView->fitInView(ui->graphicsView->scene()->sceneRect(), Qt::KeepAspectRatio);
}

//displays single color channels of the image (handled by an intensitymap)
void MainWindow::displayChannelIntensity() {
    if(input.isNull())
        return;

    IntensityMap temp;
    if(ui->radioButton_displayRed->isChecked())
        temp = IntensityMap(input, IntensityMap::AVERAGE, true, false, false, false);
    else if(ui->radioButton_displayGreen->isChecked())
        temp = IntensityMap(input, IntensityMap::AVERAGE, false, true, false, false);
    else if(ui->radioButton_displayBlue->isChecked())
        temp = IntensityMap(input, IntensityMap::AVERAGE, false, false, true, false);
    else
        temp = IntensityMap(input, IntensityMap::AVERAGE, false, false, false, true);

    this->channelIntensity = temp.convertToQImage();
    preview(0);
}

//automatically update the preview if the calculation took only a certain amount of time
//in milliseconds, e.g. 500 (0.5 seconds)
//this Slot is for parameter input fields/buttons in the gui
void MainWindow::autoUpdate() {
    if(!ui->checkBox_autoUpdate->isChecked() || !ui->checkBox_autoUpdate->isEnabled())
        return;

    int autoUpdateThreshold_ms = ui->doubleSpinBox_autoUpdateThreshold->value() * 1000.0;

    switch(ui->tabWidget->currentIndex()) {
    case 0:
        break;
    case 1:
        if(lastCalctime_normal < autoUpdateThreshold_ms)
            calcNormalAndPreview();
        break;
    case 2:
        if(lastCalctime_specular < autoUpdateThreshold_ms)
            calcSpecAndPreview();
        break;
    case 3:
        if(lastCalctime_displace < autoUpdateThreshold_ms)
            calcDisplaceAndPreview();
        break;
    case 4:
        if(lastCalctime_ssao < autoUpdateThreshold_ms)
            calcSsaoAndPreview();
        break;
    default:
        break;
    }
}

//generate a message that shows the elapsed time of a calculation process
//example output: "calculated normalmap (1.542 seconds)"
QString MainWindow::generateElapsedTimeMsg(int calcTimeMs, QString mapType) {
    double calcTimeS = (double)calcTimeMs / 1000.0;

    QString elapsedTimeMsg("calculated ");
    elapsedTimeMsg.append(mapType);
    elapsedTimeMsg.append(" (");
    elapsedTimeMsg.append(QString::number(calcTimeS));
    elapsedTimeMsg.append(" seconds)");
    return elapsedTimeMsg;
}

void MainWindow::openExportFolder() {
    QDesktopServices::openUrl(exportPath);
}

//display the last calculation time in the statusbar
void MainWindow::displayCalcTime(int calcTime_ms, QString mapType, int duration_ms) {
    std::cout << mapType.toStdString() << " for item " << loadedImagePath.fileName().toStdString()
              << " calculated, it took " << calcTime_ms << "ms" << std::endl;
    ui->statusBar->clearMessage();
    QString msg = generateElapsedTimeMsg(calcTime_ms, mapType);
    ui->statusBar->showMessage(msg, duration_ms);
    ui->label_autoUpdate_lastCalcTime->setText("(Last Calc. Time: " + QString::number((double)calcTime_ms / 1000.0) + "s)");
    
    if(calcTime_ms < ui->doubleSpinBox_autoUpdateThreshold->value() * 1000) {
        //calcTime was below the threshold, set textcolor to green
        ui->label_autoUpdate_lastCalcTime->setStyleSheet("QLabel {color: #00AA00;}");
    }
    else {
        //calcTime was above threshold, set textcolor to red to signal user the time was too long for autoupdate
        ui->label_autoUpdate_lastCalcTime->setStyleSheet("QLabel {color: red;}");
    }
}

void MainWindow::enableAutoupdate(bool on) {
    ui->checkBox_autoUpdate->setEnabled(on);
    ui->label_autoUpdate_lastCalcTime->setEnabled(on);
    ui->label_autoUpdate_text->setEnabled(on);
    ui->doubleSpinBox_autoUpdateThreshold->setEnabled(on);
}

//add single image to queue
void MainWindow::addImageToQueue(QUrl url) {
    QueueItem *item = new QueueItem(url, url.fileName(), ui->listWidget_queue, 0);
    ui->listWidget_queue->addItem(item);
}

//add multiple images to queue
void MainWindow::addImageToQueue(QList<QUrl> urls) {
    for(int i = 0; i < urls.size(); i++) {
        addImageToQueue(urls.at(i));
    }
}

void MainWindow::removeImagesFromQueue() {
    qDeleteAll(ui->listWidget_queue->selectedItems());
}

void MainWindow::queueItemDoubleClicked(QListWidgetItem* item) {
    //load image that was doubleclicked
    load(((QueueItem*)item)->getUrl());
}

//calculates the size preview text (e.g. "1024 x 1024 px")
void MainWindow::normalmapSizeChanged() {
    int sizePercent = ui->spinBox_normalmapSize->value();
    QString text = QString::number(calcPercentage(input.width(), sizePercent));
    text.append(" x ");
    text.append(QString::number(calcPercentage(input.height(), sizePercent)));
    text.append(" px");
    ui->label_normalmapSize->setText(text);
}

int MainWindow::calcPercentage(int value, int percentage) {
    return (int) (((double)value / 100.0) * percentage);
}

void MainWindow::showAboutDialog() {
    AboutDialog *dialog = new AboutDialog(this);
    dialog->show();
}

//connects gui buttons with Slots in this class
void MainWindow::connectSignalSlots() {
    //connect signals/slots
    //load/save/open export folder
    connect(ui->pushButton_load, SIGNAL(clicked()), this, SLOT(loadUserFilePath()));
    connect(ui->pushButton_save, SIGNAL(clicked()), this, SLOT(saveUserFilePath()));
    connect(ui->pushButton_openExportFolder, SIGNAL(clicked()), this, SLOT(openExportFolder()));
    //zoom
    connect(ui->pushButton_zoomIn, SIGNAL(clicked()), this, SLOT(zoomIn()));
    connect(ui->pushButton_zoomOut, SIGNAL(clicked()), this, SLOT(zoomOut()));
    connect(ui->pushButton_resetZoom, SIGNAL(clicked()), this, SLOT(resetZoom()));
    connect(ui->pushButton_fitInView, SIGNAL(clicked()), this, SLOT(fitInView()));
    //calculate
    connect(ui->pushButton_calcNormal, SIGNAL(clicked()), this, SLOT(calcNormalAndPreview()));
    connect(ui->pushButton_calcSpec, SIGNAL(clicked()), this, SLOT(calcSpecAndPreview()));
    connect(ui->pushButton_calcDisplace, SIGNAL(clicked()), this, SLOT(calcDisplaceAndPreview()));
    connect(ui->pushButton_calcSsao, SIGNAL(clicked()), this, SLOT(calcSsaoAndPreview()));
    //switch between tabs
    connect(ui->tabWidget, SIGNAL(tabBarClicked(int)), this, SLOT(preview(int)));
    //display channel intensity
    connect(ui->checkBox_displayChannelIntensity, SIGNAL(clicked(bool)), this, SLOT(preview()));
    connect(ui->checkBox_displayChannelIntensity, SIGNAL(clicked(bool)), ui->radioButton_displayRed, SLOT(setEnabled(bool)));
    connect(ui->checkBox_displayChannelIntensity, SIGNAL(clicked(bool)), ui->radioButton_displayGreen, SLOT(setEnabled(bool)));
    connect(ui->checkBox_displayChannelIntensity, SIGNAL(clicked(bool)), ui->radioButton_displayBlue, SLOT(setEnabled(bool)));
    connect(ui->checkBox_displayChannelIntensity, SIGNAL(clicked(bool)), ui->radioButton_displayAlpha, SLOT(setEnabled(bool)));

    connect(ui->radioButton_displayRed, SIGNAL(clicked()), this, SLOT(displayChannelIntensity()));
    connect(ui->radioButton_displayGreen, SIGNAL(clicked()), this, SLOT(displayChannelIntensity()));
    connect(ui->radioButton_displayBlue, SIGNAL(clicked()), this, SLOT(displayChannelIntensity()));
    connect(ui->radioButton_displayAlpha, SIGNAL(clicked()), this, SLOT(displayChannelIntensity()));
    connect(ui->checkBox_displayChannelIntensity, SIGNAL(clicked()), this, SLOT(displayChannelIntensity()));
    //autoupdate after changed values
    // spec autoupdate
    connect(ui->doubleSpinBox_spec_redMul, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_spec_greenMul, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_spec_blueMul, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_spec_alphaMul, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_spec_scale, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->comboBox_mode_spec, SIGNAL(currentIndexChanged(int)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_spec_contrast, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    // normal autoupdate
    connect(ui->checkBox_useRed_normal, SIGNAL(clicked()), this, SLOT(autoUpdate()));
    connect(ui->checkBox_useGreen_normal, SIGNAL(clicked()), this, SLOT(autoUpdate()));
    connect(ui->checkBox_useBlue_normal, SIGNAL(clicked()), this, SLOT(autoUpdate()));
    connect(ui->checkBox_useAlpha_normal, SIGNAL(clicked()), this, SLOT(autoUpdate()));
    connect(ui->comboBox_mode_normal, SIGNAL(currentIndexChanged(int)), this, SLOT(autoUpdate()));
    connect(ui->comboBox_method, SIGNAL(currentIndexChanged(int)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_strength, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->checkBox_tileable, SIGNAL(clicked()), this, SLOT(autoUpdate()));
    connect(ui->checkBox_invertHeight, SIGNAL(clicked()), this, SLOT(autoUpdate()));
    connect(ui->spinBox_normalmapSize, SIGNAL(valueChanged(int)), this, SLOT(autoUpdate()));
    connect(ui->checkBox_keepLargeDetail, SIGNAL(clicked()), this, SLOT(autoUpdate()));
    connect(ui->spinBox_largeDetailScale, SIGNAL(valueChanged(int)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_largeDetailHeight, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    // displcacement autoupdate
    connect(ui->doubleSpinBox_displace_redMul, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_displace_greenMul, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_displace_blueMul, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_displace_scale, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    connect(ui->comboBox_mode_displace, SIGNAL(currentIndexChanged(int)), this, SLOT(autoUpdate()));
    connect(ui->doubleSpinBox_displace_contrast, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    // ssao autoupdate
    connect(ui->doubleSpinBox_ssao_size, SIGNAL(valueChanged(double)), this, SLOT(autoUpdate()));
    //graphicsview drag and drop
    connect(ui->graphicsView, SIGNAL(singleImageDropped(QUrl)), this, SLOT(loadSingleDropped(QUrl)));
    connect(ui->graphicsView, SIGNAL(multipleImagesDropped(QList<QUrl>)), this, SLOT(loadMultipleDropped(QList<QUrl>)));
    //graphicsview rightclick/middleclick/zoom
    connect(ui->graphicsView, SIGNAL(rightClick()), this, SLOT(resetZoom()));
    connect(ui->graphicsView, SIGNAL(middleClick()), this, SLOT(fitInView()));
    connect(ui->graphicsView, SIGNAL(zoomIn()), this, SLOT(zoomIn()));
    connect(ui->graphicsView, SIGNAL(zoomOut()), this, SLOT(zoomOut()));
    //queue (item widget)
    connect(ui->pushButton_removeImagesFromQueue, SIGNAL(clicked()), this, SLOT(removeImagesFromQueue()));
    connect(ui->pushButton_processQueue, SIGNAL(clicked()), this, SLOT(processQueue()));
    connect(ui->pushButton_stopProcessingQueue, SIGNAL(clicked()), this, SLOT(stopProcessingQueue()));
    connect(ui->pushButton_changeOutputPath_Queue, SIGNAL(clicked()), this, SLOT(changeOutputPathQueue()));
    connect(ui->buttonGroup_exportFolder, SIGNAL(buttonClicked(int)), this, SLOT(updateQueueExportOptions()));
    connect(ui->listWidget_queue, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(queueItemDoubleClicked(QListWidgetItem*)));
    //normalmap size preview text
    connect(ui->spinBox_normalmapSize, SIGNAL(valueChanged(int)), this, SLOT(normalmapSizeChanged()));
    //"About" button
    connect(ui->pushButton_about, SIGNAL(clicked()), this, SLOT(showAboutDialog()));
}

void MainWindow::hideAdvancedSettings() {
    //Normalmap
    //"Alpha" checkbox
    ui->checkBox_useAlpha_normal->setVisible(false);
    connect(ui->checkBox_advanced_normal, SIGNAL(clicked(bool)), ui->checkBox_useAlpha_normal, SLOT(setVisible(bool)));
    //"Average/Max" combobox
    ui->comboBox_mode_normal->setVisible(false);
    connect(ui->checkBox_advanced_normal, SIGNAL(clicked(bool)), ui->comboBox_mode_normal, SLOT(setVisible(bool)));
    //"Method" label and combobox
    ui->comboBox_method->setVisible(false);
    connect(ui->checkBox_advanced_normal, SIGNAL(clicked(bool)), ui->comboBox_method, SLOT(setVisible(bool)));
    ui->label_method_normal->setVisible(false);
    connect(ui->checkBox_advanced_normal, SIGNAL(clicked(bool)), ui->label_method_normal, SLOT(setVisible(bool)));
}
