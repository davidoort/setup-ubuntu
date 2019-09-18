#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include "opencv2/imgcodecs.hpp"
#include <opencv2/highgui.hpp>
#include <opencv2/ml.hpp>
#include <math.h>

using namespace cv;
using namespace cv::ml;

#include "ros/ros.h"
#include "track/Cone.h"
#include "track/Cones.h"
#include "track/Line.h"
#include "car/Location.h"

using namespace std;

class TrackFinder {
public:
  ros::NodeHandle nodeHandle;
  ros::Subscriber car_loc_subs;
  ros::Subscriber subscriber;
  ros::Publisher publisher;

  track::Point car_location;
  std::vector<track::Point> ordered_cline;

  TrackFinder() {
    // Initialize the publisher on the '/car/targetline' topic
    publisher = nodeHandle.advertise<track::Line>("/car/targetline", 1000);
  }

  void start() {
    // Start listening on the '/car/camera' topic
    car_loc_subs = nodeHandle.subscribe("/car/location", 1000, &TrackFinder::storeCarLoc, this);
    subscriber = nodeHandle.subscribe("/car/camera", 1000, &TrackFinder::didReceiveCones, this);
  }

  void storeCarLoc(const car::Location::ConstPtr& location){
    car_location = location->location;
    // ROS_INFO("Car location: %f, %f", car_location.loc.)
  }

  void didReceiveCones(const track::Cones::ConstPtr& cones) {

    // Calculate the centerline
    track::Line centerLine = findCenterLine(cones->cones);

    // Publish the centerline
    publisher.publish(centerLine);
  }

  track::Line findCenterLine(std::vector<track::Cone> visibleCones) {

    bool visualise = true;

    int prod = 1;


    int n_cones = visibleCones.size();
    
    if (n_cones > 0) {
      // Create labels and coordinates
      int seen_cones_color[n_cones];
      float seen_cones_coords[n_cones][2];
      float seen_cones_x[n_cones];
      float seen_cones_y[n_cones];
      float seen_cones_abs_x[n_cones];
      float seen_cones_abs_y[n_cones];

      // Absolute coordinates
      for (int i=0; i<n_cones; i++){
        seen_cones_abs_x[i] = visibleCones[i].position.x*prod;
        seen_cones_abs_y[i] = visibleCones[i].position.y*prod;
      }

      // From abs to rel coordinates
      float x_min = *std::min_element(seen_cones_abs_x, seen_cones_abs_x+n_cones);     
      float y_min = *std::min_element(seen_cones_abs_y, seen_cones_abs_y+n_cones);



      // Relative coordinates
      for (int i=0; i<n_cones; i++) {
        seen_cones_color[i] = visibleCones[i].color*2-1;
        // seen_cones_color.push_back(visibleCones[i].color);
        seen_cones_coords[i][0] = (visibleCones[i].position.x - x_min)*prod;
        seen_cones_coords[i][1] = (visibleCones[i].position.y - y_min)*prod;
        seen_cones_x[i] = (visibleCones[i].position.x - x_min)*prod;
        seen_cones_y[i] = (visibleCones[i].position.y - y_min)*prod;
      };

      // Set up training data
      cv::Mat trainingDataMat(n_cones, 2, CV_32FC1, seen_cones_coords);
      cv::Mat labelsMat(n_cones, 1, CV_32SC1, seen_cones_color);

      // Train the SVM
      cv::Ptr<cv::ml::SVM> svm = cv::ml::SVM::create();
      svm->setType(cv::ml::SVM::C_SVC);
      svm->setKernel(cv::ml::SVM::INTER);
      svm->setTermCriteria(cv::TermCriteria(cv::TermCriteria::MAX_ITER, 100, 1e-6));
      svm->train(trainingDataMat, cv::ml::ROW_SAMPLE, labelsMat);
    

      std::vector<track::Point> centerLine;


      // Find the Centre Line as generated by the SVM
      // Set map boundaries to separate cones
      float x_max_rel = *std::max_element(seen_cones_x, seen_cones_x+n_cones) - *std::min_element(seen_cones_x, seen_cones_x+n_cones);     
      float y_max_rel = *std::max_element(seen_cones_y, seen_cones_y+n_cones) - *std::min_element(seen_cones_y, seen_cones_y+n_cones);

      for (int i=0; i<n_cones; i++){
        ROS_INFO("Coords cones:%f, %f", seen_cones_abs_x[i], seen_cones_abs_y[i]);
      }


      // create empty " map "
      cv::Mat map = cv::Mat::zeros(y_max_rel,x_max_rel,CV_8UC3);

      int initial_guess, final_guess;
      float x_coord, y_coord;
      int prediction, prev_prediction;

      ROS_INFO("x_min, y_min: %f, %f", x_min, y_min);
      ROS_INFO("x_max, y_max: %f, %f", x_max_rel, y_max_rel);

      float discretisation_y = y_max_rel/10;
      float discretisation_x = x_max_rel/10;



      // Get centerline

      for (float i = 1; i < y_max_rel; i += discretisation_y){     // loop through columns, omit first entry
        for (float j = 1; j < x_max_rel; j += discretisation_x){ // loop through rows, omit first entry
          // {
          //     if (i == 1){
          //         cv::Mat firstSampleMate = (cv::Mat_<float>(1,2) << j-1, i-1);
          //         float prev_response = svm->predict(firstSampleMate);
          //     } else{
          //         Mat sampleMat = (Mat_<float>(1,2) << j,i );
          //         float prev_response = response;
          //         float response = svm->predict(sampleMat);
          //     }
          //     if (response == prev_response){
          //         continue;
          //     } else if (response != prev_response){
          //         track::Point pt;
          //         pt.x = i;
          //         pt.y = j;
          //         centerLine.push_back(pt);
          //     }

          if (j== 1){
            x_coord = j;
            y_coord = i;
            cv::Mat predict_coord = (cv::Mat_<float>(1,2) << x_coord-discretisation_x, y_coord-discretisation_y);
            prev_prediction = svm->predict(predict_coord);

            cv::Mat predict_coord2 = (cv::Mat_<float>(1,2) << x_coord, y_coord);
            prediction = svm->predict(predict_coord2);

          } else {
            x_coord = j;
            y_coord = i;
            cv::Mat predict_coord = (cv::Mat_<float>(1,2) << x_coord, y_coord);
            prediction = svm->predict(predict_coord);
          }

          if (prediction == prev_prediction){
            continue;
          } else {
            if (j != 0){
              track::Point pt;
              pt.x = x_coord + x_min;
              pt.y = y_coord + y_min;
              centerLine.push_back(pt);
              prev_prediction = prediction;
            }
          }
        }
      }



      // order centerline points

      // Get car coordinates
      track::Point car_coords = car_location;


      for (int i =0; i<centerLine.size(); i++){
        ROS_INFO("Point: %f, %f", centerLine[i].x, centerLine[i].y);
      }
      // ROS_INFO("Car Location: %f, %f", car_coords.x, car_coords.y);


      float calc_dis;
      float calc_dis_prev = 1000;
      track::Point closest_point;
      int closest_point_idx;
    
      for (int i=0; i<centerLine.size(); i++){
        calc_dis = sqrt(pow((centerLine[i].x - car_coords.x), 2.0) +
        pow((centerLine[i].y - car_coords.y), 2.0));

        // ROS_INFO("Dists for %f, %f: %f", centerLine[i].x, centerLine[i].y, calc_dis);

        if (calc_dis < calc_dis_prev){
          calc_dis_prev = calc_dis;
          closest_point_idx = i;
          closest_point = centerLine[i];
        }
      }


      ordered_cline.push_back(closest_point);

      centerLine.erase(centerLine.begin() + closest_point_idx);

      order_cline(centerLine, closest_point, centerLine.size());

      // ROS_INFO("Closest point: %f, %f", ordered_cline[0].x, ordered_cline[0].y);


      if (visualise){
        int width = 512, height = 512;
        Mat image = Mat::zeros(height, width, CV_8UC3);

        Vec3b green(0, 255, 0), blue(255, 0, 0), other(0, 0, 255);

        // Show the decision regions given by the SVM
        for (int i = -y_min; i < image.rows -y_min; ++i)
        {
          for (int j = -x_min; j < image.cols -x_min; ++j)
          {
            Mat sampleMat = (Mat_<float>(1, 2) << j, i);
            float response = svm->predict(sampleMat);

            if (response == 1)
                image.at<Vec3b>(i+y_min, j+x_min) = green;
            else if (response == -1)
                image.at<Vec3b>(i+y_min, j+x_min) = blue;
          }
        }

        int thickness = -1;
        int lineType = 8;


        // Show centerline

        for (float i=0; i<ordered_cline.size(); i++){
          circle(image, Point(ordered_cline[i].x, ordered_cline[i].y), 5, Vec3b(255*i/ordered_cline.size(), 255*i/ordered_cline.size(), 255*i/ordered_cline.size()), thickness*i, lineType);
        }

        // Show the training data

        for (int i=0; i<n_cones; i++){
          circle(image, Point(seen_cones_coords[i][0]+x_min, seen_cones_coords[i][1]+y_min), 5, other, thickness, lineType);
        }

        // Show car
        circle(image, Point(car_coords.x, car_coords.y), 5, Vec3b(0, 0, 0), thickness, lineType);

        std::cout << centerLine.size();

        // show image to the user
        imshow("SVM Simple Example", image); 
        waitKey(0);
      }

      
      track::Line cline;

      cline.points = ordered_cline;
      // cline.points = centerLine;

      return cline;
    } else {
      track::Line cline;
      return cline;
    }
  }


    //////////////////////////////////////////////////////////
    // /////////////////Separation of codes///////////////////
    //////////////////////////////////////////////////////////

    // if (test_sim){
    // // // Data for visual representation
    // // int width = 512, height = 512;
    // // Mat image = Mat::zeros(height, width, CV_8UC3);

    // // // Set up training data
    // // int labels[6] = { 1, 1, 1, -1, -1, -1 };

    // // Mat labelsMat(6, 1, CV_32S, labels);

    // // float trainingData[6][2] = { { 100, 150 }, { 100, 450 }, { 200, 300 }, { 300, 150 }, { 300, 450 }, { 400, 300} };
    // // Mat trainingDataMat(6, 2, CV_32FC1, trainingData);

    // // // Set up SVM's parameters
    // // Ptr<SVM> svm = SVM::create();
    // // svm->setType(SVM::C_SVC);
    // // svm->setKernel(SVM::POLY);
    // // svm->setDegree(2.0);
    // // svm->setTermCriteria(TermCriteria(TermCriteria::MAX_ITER, 100, 1e-6));

    // // // Train the SVM with given parameters
    // // Ptr<TrainData> td = TrainData::create(trainingDataMat, ROW_SAMPLE, labelsMat);
    // // svm->train(td);

    // // // Or train the SVM with optimal parameters
    // // //svm->trainAuto(td);

    // // Vec3b green(0, 255, 0), blue(255, 0, 0);
    // // // Show the decision regions given by the SVM
    // // for (int i = 0; i < image.rows; ++i){
    // //     for (int j = 0; j < image.cols; ++j)
    // //     {
    // //       Mat sampleMat = (Mat_<float>(1, 2) << j, i);
    // //       float response = svm->predict(sampleMat);

    // //       if (response == 1)
    // //           image.at<Vec3b>(i, j) = green;
    // //       else if (response == -1)
    // //           image.at<Vec3b>(i, j) = blue;
    // //     }
    // // }
    // // // Show the training data
    // // int thickness = -1;
    // // int lineType = 8;
    // // //circle(image, Point(501, 10), 5, Scalar(0, 0, 0), thickness, lineType);
    // // //circle(image, Point(255, 10), 5, Scalar(255, 255, 255), thickness, lineType);
    // // //circle(image, Point(501, 255), 5, Scalar(255, 255, 255), thickness, lineType);
    // // //circle(image, Point(10, 501), 5, Scalar(255, 255, 255), thickness, lineType);

    // // // Show support vectors
    // // thickness = -1;
    // // lineType = 8;
    // // Mat sv = svm->getSupportVectors();

    // // for (int i = 0; i < sv.rows; ++i)
    // // {

    // //   ROS_INFO("Drawing circle");
    // //   const float* v = sv.ptr<float>(i);
    // //   circle(image, Point((int)v[0], (int)v[1]), 6, Scalar(255, 255, 255), thickness, lineType);
    // // }

    // // // ROS_INFO("Stored center line in result.png!");

    // // // imwrite("/var/tmp/opencv-img.png", image);        // save the image

    // // imshow("SVM Simple Example", image); // show it to the user
    // // waitKey(0);




    // //  visual representation
    // int width = 512, height = 512;
    // Mat image = Mat::zeros(height, width, CV_8UC3);

    // // Set up training data
    // int data_points = 6;
    // int labels[6] = { 1, 1, 1, -1, -1, -1 };

    // Mat labelsMat(6, 1, CV_32S, labels);

    // float trainingData[6][2] = { { 100, 150 }, { 100, 450 }, { 200, 300 }, { 300, 150 }, { 300, 450 }, { 400, 300} };
    // Mat trainingDataMat(6, 2, CV_32FC1, trainingData);

    // // Set up SVM's parameters
    // Ptr<SVM> svm = SVM::create();
    // svm->setType(SVM::C_SVC);
    // svm->setKernel(SVM::INTER);
    // // svm->setC(0.1); 
    // // svm->setGamma(0.01);
    // // svm->setDegree(5);
    // svm->setTermCriteria(TermCriteria(TermCriteria::MAX_ITER, 100, 1e-6));

    // // Train the SVM with given parameters
    // Ptr<TrainData> td = TrainData::create(trainingDataMat, ROW_SAMPLE, labelsMat);
    // svm->train(td);

    // // Or train the SVM with optimal parameters
    // //svm->trainAuto(td);

    // Vec3b green(0, 255, 0), blue(255, 0, 0);
    // // Show the decision regions given by the SVM
    // for (int i = 0; i < image.rows; ++i)
    // {
    //   for (int j = 0; j < image.cols; ++j)
    //   {
    //     Mat sampleMat = (Mat_<float>(1, 2) << j, i);
    //     float response = svm->predict(sampleMat);

    //     if (response == 1)
    //         image.at<Vec3b>(i, j) = green;
    //     else if (response == -1)
    //         image.at<Vec3b>(i, j) = blue;
    //   }
    // }

    // // Show the training data
    // int thickness = -1;
    // int lineType = 8;

    // for (int i=0; i<data_points; i++){
    //   circle(image, Point(trainingData[i][0], trainingData[i][1]), 5, Scalar(177.5*(1-labels[i]), 177.5*(1-labels[i]), 177.5*(1-labels[i])), thickness, lineType);
    // }
    // //circle(image, Point(501, 10), 5, Scalar(0, 0, 0), thickness, lineType);
    // //circle(image, Point(255, 10), 5, Scalar(255, 255, 255), thickness, lineType);
    // //circle(image, Point(501, 255), 5, Scalar(255, 255, 255), thickness, lineType);
    // //circle(image, Point(10, 501), 5, Scalar(255, 255, 255), thickness, lineType);

    // // Show support vectors
    // // Mat sv = svm->getSupportVectors();
    // //
    // // for (int i = 0; i < sv.rows; ++i)
    // // {
    // //   ROS_INFO("Drawing circle");
    // //   const float* v = sv.ptr<float>(i);
    // //   circle(image, Point((int)v[0], (int)v[1]), 6, Scalar(255, 255, 255), thickness, lineType);
    // // }

    // // ROS_INFO("Stored center line in result.png!");

    // // imwrite("/var/tmp/opencv-img.png", image);        // save the image

    // imshow("SVM Simple Example", image); // show it to the user
    // waitKey(0);



    // track::Line centerLine;

    // // Put one demo point in the center line
    // track::Point demoPoint;
    // demoPoint.x = 1.0;
    // demoPoint.y = 2.0;
    // std::vector<track::Point> centerLinePoints;
    // centerLinePoints.push_back(demoPoint);
    // centerLine.points = centerLinePoints;

    // track::Line cline;
    // // cline.points = centerLinePoints;

    // return cline;
    // }

    // else {
    //   track::Line empty_line;
    //   return empty_line;
    // }
  // }

  void order_cline(std::vector<track::Point> line, track::Point old_point, int size){
    if (size != 0){
      float dist_old_point;
      float dist_old_point_prev = 1000;
      int closest_point_idx;
      track::Point closest_point;

      for (int i=0; i<size; i++){
        dist_old_point = sqrt(pow((line[i].x-old_point.x), 2) + pow((line[i].y-old_point.y), 2));

        if (dist_old_point < dist_old_point_prev){
          dist_old_point_prev = dist_old_point;
          closest_point_idx = i;
          closest_point = line[i];
        }
      }

      ordered_cline.push_back(closest_point);
      line.erase(line.begin() + closest_point_idx);
      order_cline(line, closest_point, line.size());
      // ROS_INFO("Closest point is: %f, %f", closest_point.x, closest_point.y);
    }
    return;
  }
};

int main(int argc, char **argv) {

  // Init the node
  ROS_INFO("Starting 'trackfinder' node...");
  ros::init(argc, argv, "trackfinder");

  // Create a TrackFinder object
  TrackFinder trackFinder;

  // Start the feedback loop
  trackFinder.start();

  // Directly run findCenterLine()
  // vector<track::Cone> cones;
  // trackFinder.findCenterLine(cones);

  // Keep listening till Ctrl+C is pressed
  ros::spin();

  // Exit succesfully
  return 0;
}