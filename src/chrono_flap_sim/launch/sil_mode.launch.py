from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration, Command, PathJoinSubstitution
from launch_ros.actions import Node
from launch.substitutions import FindExecutable
from launch_ros.substitutions import FindPackageShare
from launch_ros.parameter_descriptions import ParameterValue


def generate_launch_description():
    declared_arguments = []

    declared_arguments.append(
        DeclareLaunchArgument(
            "bearing_friction",
            default_value="0.01",
            description="Bearing friction coefficient for the Chrono simulation.",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "control_mode",
            default_value="cascade",
            description="Control mode for the velocity PID node.",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "position_setpoint",
            default_value="0.0",
            description="Position setpoint (rad) for the velocity PID node in position_only mode.",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "enable_visualization",
            default_value="false",
            description="Enable Chrono 3D visualization (requires working Vulkan/GPU).",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "enable_rqt",
            default_value="true",
            description="Launch rqt_reconfigure for live parameter editing.",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "enable_plotjuggler",
            default_value="true",
            description="Launch PlotJuggler for time-series plotting (install: sudo apt install ros-jazzy-plotjuggler-ros).",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "seastack_h5_path",
            default_value="",
            description="Path to SEA-Stack BEM .h5 file. Empty disables SEA-Stack.",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "wave_hs_m",
            default_value="0.0",
            description="JONSWAP significant wave height in meters (0.0 = calm sea).",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "hydro_torque_clip_nm",
            default_value="0.2",
            description="Hard clip for SEA-Stack torque before Chrono application (N·m).",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "enable_rviz",
            default_value="true",
            description="Launch RViz2 for 3D visualization.",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "rviz_config",
            default_value="",
            description="Path to an RViz config file (.rviz). Leave empty to open with defaults.",
        )
    )

    declared_arguments.append(
        DeclareLaunchArgument(
            "plotjuggler_layout",
            default_value="",
            description="Path to a PlotJuggler layout file (.xml). Leave empty to open with defaults.",
        )
    )

    bearing_friction = LaunchConfiguration("bearing_friction")
    control_mode = LaunchConfiguration("control_mode")
    position_setpoint = LaunchConfiguration("position_setpoint")
    enable_visualization = LaunchConfiguration("enable_visualization")
    enable_rqt = LaunchConfiguration("enable_rqt")
    enable_plotjuggler = LaunchConfiguration("enable_plotjuggler")
    enable_rviz = LaunchConfiguration("enable_rviz")
    rviz_config = LaunchConfiguration("rviz_config")
    plotjuggler_layout = LaunchConfiguration("plotjuggler_layout")
    seastack_h5_path = LaunchConfiguration("seastack_h5_path")
    wave_hs_m = LaunchConfiguration("wave_hs_m")
    hydro_torque_clip_nm = LaunchConfiguration("hydro_torque_clip_nm")

    robot_description_content = Command(
        [
            PathJoinSubstitution([FindExecutable(name="xacro")]),
            " ",
            PathJoinSubstitution(
                [FindPackageShare("hil_odrive_ros2_control"), "description", "urdf", "motor.urdf.xacro"]
            ),
        ]
    )
    robot_description = {"robot_description": ParameterValue(robot_description_content, value_type=str)}

    robot_state_pub_node = Node(
        package="robot_state_publisher",
        executable="robot_state_publisher",
        parameters=[robot_description],
        output="both",
    )

    chrono_flap_node = Node(
        package="chrono_flap_sim",
        executable="chrono_flap_node",
        name="chrono_flap_node",
        parameters=[{
            "sil_mode": True,
            "mode": "sil",
            "bearing_friction": bearing_friction,
            "enable_visualization": enable_visualization,
            "seastack_h5_path": seastack_h5_path,
            "wave_hs_m": wave_hs_m,
            "hydro_torque_clip_nm": hydro_torque_clip_nm,
        }],
        output="both",
    )

    velocity_pid_node = Node(
        package="odrive_velocity_pid",
        executable="velocity_pid_node",
        name="velocity_pid_node",
        parameters=[{
            "control_mode": control_mode,
            "position_setpoint": position_setpoint,
        }],
        output="both",
    )

    rqt_node = Node(
        package="rqt_reconfigure",
        executable="rqt_reconfigure",
        name="rqt_reconfigure",
        output="screen",
        condition=IfCondition(enable_rqt),
    )

    plotjuggler_node = Node(
        package="plotjuggler",
        executable="plotjuggler",
        name="plotjuggler",
        arguments=["-l", plotjuggler_layout],
        output="screen",
        condition=IfCondition(enable_plotjuggler),
    )

    rviz_node = Node(
        package="rviz2",
        executable="rviz2",
        name="rviz2",
        arguments=["-d", rviz_config],
        output="screen",
        condition=IfCondition(enable_rviz),
    )

    return LaunchDescription(
        declared_arguments
        + [
            robot_state_pub_node,
            chrono_flap_node,
            velocity_pid_node,
            rqt_node,
            plotjuggler_node,
            rviz_node,
        ]
    )
