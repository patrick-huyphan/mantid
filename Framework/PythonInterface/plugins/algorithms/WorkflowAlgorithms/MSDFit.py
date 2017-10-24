# pylint: disable=no-init
from __future__ import (absolute_import, division, print_function)
from mantid.simpleapi import *
from mantid.api import *
from mantid.kernel import *
from six.moves import range  # pylint: disable=redefined-builti


class MSDFit(DataProcessorAlgorithm):
    _output_fit_ws = None
    _model = None
    _spec_range = None
    _x_range = None
    _input_ws = None
    _output_param_ws = None
    _output_msd_ws = None

    def category(self):
        return 'Workflow\\MIDAS'

    def summary(self):
        return 'Fits Intensity vs Q for 3 models to obtain the mean squared displacement.'

    def PyInit(self):
        self.declareProperty(MatrixWorkspaceProperty('InputWorkspace', '', direction=Direction.Input),
                             doc='Sample input workspace')
        self.declareProperty(name='Model', defaultValue='Gauss',
                             validator=StringListValidator(['Gauss', 'Peters', 'Yi']),
                             doc='Model options : Gauss, Peters, Yi')

        self.declareProperty(name='XStart', defaultValue=0.0,
                             doc='Start of fitting range')
        self.declareProperty(name='XEnd', defaultValue=0.0,
                             doc='End of fitting range')

        self.declareProperty(name='SpecMin', defaultValue=0,
                             doc='Start of spectra range to be fit')
        self.declareProperty(name='SpecMax', defaultValue=0,
                             doc='End of spectra range to be fit')

        self.declareProperty(MatrixWorkspaceProperty('OutputWorkspace', '', direction=Direction.Output),
                             doc='Output mean squared displacement')

        self.declareProperty(ITableWorkspaceProperty('ParameterWorkspace', '',
                                                     direction=Direction.Output,
                                                     optional=PropertyMode.Optional),
                             doc='Output fit parameters table')

        self.declareProperty(WorkspaceGroupProperty('FitWorkspaces', '',
                                                    direction=Direction.Output,
                                                    optional=PropertyMode.Optional),
                             doc='Output fitted workspaces')

    def validateInputs(self):
        issues = dict()

        workspace = self.getProperty('InputWorkspace').value
        x_data = workspace.readX(0)

        # Validate X axis fitting range
        x_min = self.getProperty('XStart').value
        x_max = self.getProperty('XEnd').value

        if x_min > x_max:
            msg = 'XStart must be less then XEnd'
            issues['XStart'] = msg
            issues['XEnd'] = msg

        if x_min < x_data[0]:
            issues['XStart'] = 'Must be greater than minimum X value in workspace'

        if x_max > x_data[-1]:
            issues['XEnd'] = 'Must be less than maximum X value in workspace'

        # Validate spectra fitting range
        spec_min = self.getProperty('SpecMin').value
        spec_max = self.getProperty('SpecMax').value

        if spec_min < 0:
            issues['SpecMin'] = 'Minimum spectrum number must be greater than or equal to 0'

        if spec_max > workspace.getNumberHistograms():
            issues['SpecMax'] = 'Maximum spectrum number must be less than number of spectra in workspace'

        if spec_min > spec_max:
            msg = 'SpecMin must be less then SpecMax'
            issues['SpecMin'] = msg
            issues['SpecMax'] = msg

        return issues

    def PyExec(self):
        self._setup()
        progress = Progress(self, 0.0, 0.05, 3)
        self._original_ws = self._input_ws

        RenameWorkspace(InputWorkspace=self._input_ws,
                        OutputWorkspace=self._input_ws + "_" + self._model, EnableLogging=False)

        self._input_ws = self._input_ws + "_" + self._model
        input_params = [self._input_ws + ',i%d' % i for i in range(self._spec_range[0],
                                                                   self._spec_range[1] + 1)]

        # Fit line to each of the spectra
        if self._model == 'Gauss':
            logger.information('Model : Gaussian approximation')
            function = 'name=MsdGauss, Height=1.0, MSD=0.1'
            function += ',constraint=(Height>0.0, MSD>0.0)'
            params_list = ['Height', 'MSD']
        elif self._model == 'Peters':
            logger.information('Model : Peters & Kneller')
            function = 'name=MsdPeters, Height=1.0, MSD=1.0, Beta=1.0'
            function += ',constraint=(Height>0.0, MSD>0.0, 100.0>Beta>0.3)'
            params_list = ['Height', 'MSD', 'Beta']
        elif self._model == 'Yi':
            logger.information('Model : Yi et al')
            function = 'name=MsdYi, Height=1.0, MSD=1.0, Sigma=0.1'
            function += ',constraint=(Height>0.0, MSD>0.0, Sigma>0.0)'
            params_list = ['Height', 'MSD', 'Sigma']
        else:
            raise ValueError('No Model defined')

        input_params = ';'.join(input_params)
        progress.report('Sequential fit')
        PlotPeakByLogValue(Input=input_params,
                           OutputWorkspace=self._output_msd_ws,
                           Function=function,
                           StartX=self._x_range[0],
                           EndX=self._x_range[1],
                           FitType='Sequential',
                           CreateOutput=True)

        DeleteWorkspace(self._output_msd_ws + '_NormalisedCovarianceMatrices', EnableLogging=False)
        DeleteWorkspace(self._output_msd_ws + '_Parameters', EnableLogging=False)
        RenameWorkspace(InputWorkspace=self._output_msd_ws, OutputWorkspace=self._output_param_ws, EnableLogging=False)

        progress.report('Create output files')

        # Create workspaces for each of the parameters
        parameter_ws_group = []
        for par in params_list:
            ws_name = self._output_msd_ws + '_' + par
            parameter_ws_group.append(ws_name)
            ConvertTableToMatrixWorkspace(InputWorkspace=self._output_param_ws, OutputWorkspace=ws_name,
                                          ColumnX='axis-1', ColumnY=par, ColumnE=par + '_Err', EnableLogging=False)

        AppendSpectra(InputWorkspace1=self._output_msd_ws + '_' + params_list[0],
                      InputWorkspace2=self._output_msd_ws + '_' + params_list[1],
                      ValidateInputs=False, EnableLogging=False, OutputWorkspace=self._output_msd_ws)

        if len(params_list) > 2:
            AppendSpectra(InputWorkspace1=self._output_msd_ws,
                          InputWorkspace2=self._output_msd_ws + '_' + params_list[2],
                          ValidateInputs=False, EnableLogging=False, OutputWorkspace=self._output_msd_ws)
        for par in params_list:
            DeleteWorkspace(self._output_msd_ws + '_' + par, EnableLogging=False)

        progress.report('Change axes')
        # Sort ascending x
        SortXAxis(InputWorkspace=self._output_msd_ws, OutputWorkspace=self._output_msd_ws, EnableLogging=False)
        # Create a new x axis for the Q and Q**2 workspaces
        xunit = mtd[self._output_msd_ws].getAxis(0).setUnit('Label')
        xunit.setLabel('Temperature', 'K')
        # Create a new vertical axis for the Q and Q**2 workspaces
        y_axis = NumericAxis.create(len(params_list))
        for idx in range(len(params_list)):
            y_axis.setValue(idx, idx)
        mtd[self._output_msd_ws].replaceAxis(1, y_axis)

        # Rename fit workspace group
        original_fit_ws_name = self._output_msd_ws + '_Workspaces'
        if original_fit_ws_name != self._output_fit_ws:
            RenameWorkspace(InputWorkspace=self._output_msd_ws + '_Workspaces',
                            OutputWorkspace=self._output_fit_ws, EnableLogging=False)

        # Add sample logs to output workspace
        CopyLogs(InputWorkspace=self._input_ws, OutputWorkspace=self._output_msd_ws, EnableLogging=False)
        CopyLogs(InputWorkspace=self._input_ws, OutputWorkspace=self._output_fit_ws, EnableLogging=False)
        RenameWorkspace(InputWorkspace=self._input_ws, OutputWorkspace=self._original_ws, EnableLogging=False)

        self.setProperty('OutputWorkspace', self._output_msd_ws)
        self.setProperty('ParameterWorkspace', self._output_param_ws)
        self.setProperty('FitWorkspaces', self._output_fit_ws)

    def _setup(self):
        """
        Gets algorithm properties.
        """
        self._input_ws = self.getPropertyValue('InputWorkspace')
        self._model = self.getPropertyValue('Model')
        self._output_msd_ws = self.getPropertyValue('OutputWorkspace')

        self._output_param_ws = self.getPropertyValue('ParameterWorkspace')
        if self._output_param_ws == '':
            self._output_param_ws = self._output_msd_ws + '_Parameters'

        self._output_fit_ws = self.getPropertyValue('FitWorkspaces')
        if self._output_fit_ws == '':
            self._output_fit_ws = self._output_msd_ws + '_Workspaces'

        self._x_range = [self.getProperty('XStart').value,
                         self.getProperty('XEnd').value]

        self._spec_range = [self.getProperty('SpecMin').value,
                            self.getProperty('SpecMax').value]


AlgorithmFactory.subscribe(MSDFit)
