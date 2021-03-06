within QuadrotorProject;

model BlockChart
  //Import libraries and config
  import QuadrotorProject.Config.*;
  import Modelica.Blocks.Interfaces.RealInput;
  import Modelica.Blocks.Interfaces.RealOutput;
  import Modelica.SIunits;
  // Include the blocks
  RefValues refValues annotation(Placement(visible = true, transformation(origin = {-116, 40}, extent = {{-10, -10}, {10, 10}}, rotation = 0)));
  Controller controller annotation(Placement(visible = true, transformation(origin = {-68, 36}, extent = {{-10, -10}, {10, 10}}, rotation = 0)));
  Sensors sensors annotation(Placement(visible = true, transformation(origin = {-22, -6}, extent = {{-10, -10}, {10, 10}}, rotation = 0)));
  Quadrotor quadrotor annotation(Placement(visible = true, transformation(origin = {4, 36}, extent = {{-10, -10}, {10, 10}}, rotation = 0)));
  Controller_C controller_C annotation(Placement(visible = true, transformation(origin = {-68, 66}, extent = {{-10, -10}, {10, 10}}, rotation = 0)));
equation
  connect(controller.ctrl, quadrotor.ctrl) annotation(Line(points = {{-57, 36}, {-8, 36}, {-8, 36}, {-8, 36}}, color = {0, 0, 127}));
  connect(sensors.sens, controller_C.sens) annotation(Line(points = {{-33, -6}, {-92, -6}, {-92, 62}, {-80, 62}}, color = {0, 0, 127}));
  connect(controller_C.ref, refValues.ref) annotation(Line(points = {{-80, 70}, {-98, 70}, {-98, 40}, {-105, 40}}, color = {0, 0, 127}));
  connect(quadrotor.plantOut, sensors.plantOut) annotation(Line(points = {{15, 36}, {36, 36}, {36, -6}, {-10, -6}, {-10, -6}, {-10, -6}}, color = {0, 0, 127}));
  connect(sensors.sens, controller.sens) annotation(Line(points = {{-33, -6}, {-92, -6}, {-92, 32}, {-80, 32}}, color = {0, 0, 127}));
  connect(refValues.ref, controller.ref) annotation(Line(points = {{-105, 40}, {-80, 40}, {-80, 40}, {-80, 40}}, color = {0, 0, 127}));
  // Connect the blocks
  annotation(experiment(StartTime = 0, StopTime = 10), Documentation(info = "<html><h1 class=\"heading\">BLOCK CHART TO CONNECT ALL BLOCKS TOGETHER</h1><p>This should not be necessary to modify.</p></html>"));
end BlockChart;