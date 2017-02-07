#include <cmath>
#include <iostream>
#include <chrono>

#include <QQuickItemGrabResult>

#include <std_msgs/Empty.h>

#include <visualization_msgs/MarkerArray.h>
#include <visualization_msgs/Marker.h>
#include <geometry_msgs/Point.h>

#include "ros.h"

using namespace std;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

RosPositionController::RosPositionController(QQuickItem *parent):
    _origin(nullptr),
    _pixel2meter(1)
{

    connect(this, SIGNAL(onMsgReceived(double, double)),
            this, SLOT(updatePos(double, double)));

}

void RosPositionController::onIncomingPose(const geometry_msgs::PoseStamped &pose)
{
    emit onMsgReceived(pose.pose.position.x, pose.pose.position.y);
}

void RosPositionController::updatePos(double x, double y)
{
    double px,py;
    if (_origin) {
        px = x / _pixel2meter + _origin->x();
        py = -y / _pixel2meter + _origin->y();
    }
    else {
        px = x / _pixel2meter;
        py = -y / _pixel2meter;
    }

    setX(px);
    setY(py);

    emit onPositionChanged();

}

void RosPositionController::setTopic(QString topic)
{
    _incoming_poses = _node.subscribe(topic.toStdString(), 1, &RosPositionController::onIncomingPose,this);

    _topic = topic;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TFBroadcaster::TFBroadcaster(QQuickItem *parent):
    _active(true),
    _initialized(false),
    _running(false),
    _target(nullptr),
    _origin(nullptr),
    _frame(""),
    _parentframe(""),
    _pixel2meter(1),
    _zoffset(0)
{

}

TFBroadcaster::~TFBroadcaster()
{
    if (_running) {
        _running=false;
        _broadcaster_thread.join();
    }

}

void TFBroadcaster::setTarget(QQuickItem* target)
{

   _target = target;

   if (!_running) {
       _running = true;

       _broadcaster_thread = std::thread(&TFBroadcaster::tfPublisher, this);

   }

}

void TFBroadcaster::setFrame(QString frame)
{

        _frame = frame;
        if (!_parentframe.isEmpty()) _initialized = true;

        //cout << "Frame set to: " << _frame.toStdString() << endl;
}


void TFBroadcaster::setParentFrame(QString frame)
{

        _parentframe = frame;
        if (!_frame.isEmpty()) _initialized = true;

        //cout << "Parent frame set to: " << _parentframe.toStdString() << endl;
}

void TFBroadcaster::tfPublisher()
{
    while(_running) {
        if(_initialized && _active) {
            double x,y, theta;
            if (_origin) {
                x = (_target->mapToScene(QPoint(0,0)).x() - _origin->mapToScene(QPoint(0,0)).x()) * _pixel2meter;
                y = -(_target->mapToScene(QPoint(0,0)).y() - _origin->mapToScene(QPoint(0,0)).y()) * _pixel2meter;
                theta = -(_target->rotation() - _origin->rotation()) * M_PI/180;
            }
            else {
                x = _target->mapToScene(QPoint(0,0)).x() * _pixel2meter;
                y = -_target->mapToScene(QPoint(0,0)).y() * _pixel2meter;
                theta = -_target->rotation() * M_PI/180;
            }

            tf::Transform transform;
            transform.setOrigin( tf::Vector3(x, y, _zoffset) );

            tf::Quaternion q;
            q.setRPY(0, 0, theta);
            transform.setRotation(q);
            _br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), _parentframe.toStdString(), _frame.toStdString()));
        }
       this_thread::sleep_for(chrono::milliseconds(100));
    }

}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

ImagePublisher::ImagePublisher(QQuickItem *parent):
    _active(true),
    _target(nullptr),
    _frame(""),
    _width(0),
    _height(0),
    _topic("image"),
    _it(_node)
{

    _publisher = _it.advertise(_topic.toStdString(), 1);
}


void ImagePublisher::setTarget(QQuickItem* target)
{

   _target = target;
}

void ImagePublisher::publish() {

   // if _width or _height are 0, size is invalid, and grabToImage uses the item actual size
   QSize size(_width, _height);

   auto result = _target->grabToImage(size);
   connect(result.data(), &QQuickItemGrabResult::ready, this, [result, this] () {

           this->_rospublish(result.data()->image());

           }
           );

}

void ImagePublisher::setFrame(QString frame)
{

        _frame = frame;
        //cout << "Frame set to: " << _frame.toStdString() << endl;
}

void ImagePublisher::setTopic(QString topic)
{

    _topic = topic;
    _publisher = _it.advertise(_topic.toStdString(), 1);
}



void ImagePublisher::_rospublish(const QImage& image)
{

    if(_active) {

        auto size = image.size();
        auto img = image.convertToFormat(QImage::Format_RGBA8888);

        sensor_msgs::Image msg;
        msg.header.frame_id = _frame.toStdString();
        msg.header.stamp = ros::Time::now();
        msg.height = size.height();
        msg.width = size.width();
        msg.step  = img.bytesPerLine();
        msg.encoding = "rgba8";
        msg.data.resize(img.byteCount()); // allocate memory
        memcpy(msg.data.data(), img.constBits(), img.byteCount());

        _publisher.publish(msg);

    }

}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

const QString FootprintsPublisher::topic = "footprints";

FootprintsPublisher::FootprintsPublisher(QQuickItem *parent):
    _pixel2meter(1),
    _publisher(_node.advertise<visualization_msgs::MarkerArray>(topic.toStdString(), 1, true))
{

}

void FootprintsPublisher::setTargets(QVariantList targets)
{
    visualization_msgs::MarkerArray markers;

    cout << "Setting targets for footprint publishing. Got " << targets.length() << " of them" << endl;
    QVariantList::const_iterator i;

    int id = 0;

    for (i = targets.begin(); i != targets.end(); ++i) {
        auto item = (*i).value<QQuickItem*>();

        if(!item) {
            cerr << "One of my targets is does not exist! Skipping it." << endl;
            continue;
        }
        auto target = item->property("name").value<QString>().toStdString();
        auto boundingbox = item->property("boundingbox").value<QObject*>();
        auto vertices = boundingbox->property("vertices").value<QVariantList>();

        cout << target << endl;

        visualization_msgs::Marker marker;
        marker.header.frame_id = "/" + target;
        marker.header.stamp = ros::Time::now();
        marker.ns = "qml_items_footprints";
        marker.action = visualization_msgs::Marker::ADD;
        marker.pose.orientation.w = 1.0;

        marker.id = id++;
        marker.type = visualization_msgs::Marker::LINE_STRIP;
        marker.scale.x = 0.005; // width of the visualized line
        marker.color.b = 1.0;
        marker.color.a = 1.0;


        QVariantList::const_iterator point_it;

        double bbx=0, bby=0;

        for(point_it = vertices.begin(); point_it != vertices.end(); ++point_it) {
            geometry_msgs::Point p;
            auto point = (*point_it).value<QPointF>();
            p.x = point.x() * _pixel2meter;
            bbx += p.x;
            p.y = -point.y() * _pixel2meter;
            bby += p.y;
            p.z = 0;
            marker.points.push_back(p);
        }
        marker.points.push_back(marker.points.front());

        // compute the center of the bounding box
        bbx /= vertices.length();
        bby /= vertices.length();

        for (auto& p : marker.points) {
            p.x -= bbx;
            p.y -= bby;
        }

        markers.markers.push_back(marker);

    }

    _publisher.publish(markers);

}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void RosSignal::setTopic(QString topic)
{
    _publisher = _node.advertise<std_msgs::Empty>(topic.toStdString(), 1);

    _topic = topic;
}

void RosSignal::signal()
{
   if(_publisher.getTopic().empty()) {
       cerr << "RosSignal.signal() called without any topic." << endl;
       return;
   }

   _publisher.publish(std_msgs::Empty());
}
