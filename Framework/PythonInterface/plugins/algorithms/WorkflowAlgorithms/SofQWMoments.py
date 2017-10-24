# Algorithm to start Bayes programs
from mantid.simpleapi import *
from mantid.api import DataProcessorAlgorithm, AlgorithmFactory, MatrixWorkspaceProperty, NumericAxis, Progress
from mantid.kernel import Direction
from mantid import logger

import numpy as np


class SofQWMoments(DataProcessorAlgorithm):
    def category(self):
        return "Workflow\\MIDAS"

    def summary(self):
        return "Calculates the nth moment of y(q,w)"

    def PyInit(self):
        self.declareProperty(MatrixWorkspaceProperty("InputWorkspace", "", Direction.Input),
                             doc="Input workspace to use.")
        self.declareProperty(name='EnergyMin', defaultValue=-0.5,
                             doc='Minimum energy for fit. Default=-0.5')
        self.declareProperty(name='EnergyMax', defaultValue=0.5,
                             doc='Maximum energy for fit. Default=0.5')
        self.declareProperty(name='Scale', defaultValue=1.0,
                             doc='Scale factor to multiply y(Q,w). Default=1.0')
        self.declareProperty(MatrixWorkspaceProperty("OutputWorkspace", "", Direction.Output),
                             doc="Workspace that includes all calculated moments.")

    def PyExec(self):

        workflow_prog = Progress(self, start=0.0, end=1.0, nreports=20)
        self._setup()

        workflow_prog.report('Validating input')
        input_workspace = mtd[self._input_ws]
        num_spectra, num_w = self._CheckHistZero(self._input_ws)
        logger.information('Sample %s has %d Q values & %d w values' % (self._input_ws, num_spectra, num_w))
        self._CheckElimits([self._energy_min, self._energy_max], self._input_ws)

        workflow_prog.report('Cropping Workspace')
        input_ws = '__temp_sqw_moments_cropped'
        CropWorkspace(InputWorkspace=input_workspace, XMin=self._energy_min, XMax=self._energy_max,
                      EnableLogging=False, OutputWorkspace=input_ws)
        logger.information('Energy range is %f to %f' % (self._energy_min, self._energy_max))

        if self._factor > 0.0:
            workflow_prog.report('Scaling Workspace by factor %f' % self._factor)
            Scale(InputWorkspace=input_ws, OutputWorkspace=input_ws, Factor=self._factor,
                  Operation='Multiply', EnableLogging=False)
            logger.information('y(q,w) scaled by %f' % self._factor)

        # calculate delta x
        workflow_prog.report('Converting to point data')
        ConvertToPointData(InputWorkspace=input_ws, OutputWorkspace=input_ws, EnableLogging=False)
        x_data = np.asarray(mtd[input_ws].readX(0))
        workflow_prog.report('Creating temporary data workspace')
        x_workspace = "__temp_sqw_moments_x"
        CreateWorkspace(OutputWorkspace=x_workspace, DataX=x_data, DataY=x_data, UnitX="DeltaE", EnableLogging=False)

        # calculate moments
        workflow_prog.report('Multiplying Workspaces by moments')
        number_moments = 5
        moments = []
        for i in range(number_moments):
            moments.append(self._output_ws + '_M' + str(i))

        Multiply(LHSWorkspace=x_workspace, RHSWorkspace=input_ws, OutputWorkspace=moments[1], EnableLogging=False)

        for i in range(1,number_moments - 1):
            Multiply(LHSWorkspace=x_workspace, RHSWorkspace=moments[i], OutputWorkspace=moments[i+1], EnableLogging=False)

        workflow_prog.report('Converting to Histogram')
        histogram = ConvertToHistogram(InputWorkspace=input_ws, OutputWorkspace=input_ws, EnableLogging=False, StoreInADS=False)

        workflow_prog.report('Integrating result')
        Integration(InputWorkspace=histogram, OutputWorkspace=moments[0], EnableLogging=False)

        for moment_ws in moments:
            workflow_prog.report('Processing workspace %s' % moment_ws)
            moment_hist = ConvertToHistogram(InputWorkspace=moment_ws, StoreInADS=False, EnableLogging=False)
            integrated = Integration(InputWorkspace=moment_hist, StoreInADS=False, EnableLogging=False)
            Divide(LHSWorkspace=integrated, RHSWorkspace=moments[0], OutputWorkspace=moment_ws, EnableLogging=False)

        workflow_prog.report('Deleting Workspaces')
        DeleteWorkspace(input_ws, EnableLogging=False)
        DeleteWorkspace(x_workspace, EnableLogging=False)

        # create output workspace
        extensions = ['_M0', '_M1', '_M2', '_M3', '_M4']
        for ext in extensions:
            ws_name = self._output_ws + ext
            workflow_prog.report('Processing Workspace %s' % ext)
            transposed = Transpose(ws_name, EnableLogging=False, StoreInADS=False)
            histogram = ConvertToHistogram(transposed, EnableLogging=False, StoreInADS=False)
            ConvertUnits(InputWorkspace=histogram, OutputWorkspace=ws_name, EnableLogging=False,
                         Target="MomentumTransfer", Emode="Indirect")
            workflow_prog.report('Adding Sample logs to %s' % ws_name)
            CopyLogs(InputWorkspace=input_workspace, OutputWorkspace=ws_name, EnableLogging=False)
            AddSampleLog(Workspace=ws_name, LogName="energy_min", LogType="Number", LogText=str(self._energy_min),
                         EnableLogging=False)
            AddSampleLog(Workspace=ws_name, LogName="energy_max", LogType="Number", LogText=str(self._energy_max),
                         EnableLogging=False)
            AddSampleLog(Workspace=ws_name, LogName="scale_factor", LogType="Number", LogText=str(self._factor),
                         EnableLogging=False)

        # Group output workspace
        workflow_prog.report('Appending moments')

        appended = AppendSpectra(InputWorkspace1=self._output_ws + '_M0', InputWorkspace2=self._output_ws + '_M1',
                                 EnableLogging=False, StoreInADS=False, ValidateInputs=False)
        appended = AppendSpectra(InputWorkspace1=appended, InputWorkspace2=self._output_ws + '_M2',
                                 EnableLogging=False, StoreInADS=False)
        appended = AppendSpectra(InputWorkspace1=appended, InputWorkspace2=self._output_ws + '_M3',
                                 EnableLogging=False, StoreInADS=False)
        AppendSpectra(InputWorkspace1=appended, InputWorkspace2=self._output_ws + '_M4',
                      EnableLogging=False, OutputWorkspace=self._output_ws)

        for i in range(number_moments):
            DeleteWorkspace(self._output_ws + '_M' + str(i), EnableLogging=False)

        # Create a new vertical axis for the Q and Q**2 workspaces
        y_axis = NumericAxis.create(5)
        for idx in range(5):
            y_axis.setValue(idx, idx)
        mtd[self._output_ws].replaceAxis(1, y_axis)

        self.setProperty("OutputWorkspace", self._output_ws)

    def _setup(self):
        """
        Gets algorithm properties.
        """

        self._input_ws = self.getPropertyValue('InputWorkspace')
        self._factor = self.getProperty('Scale').value
        self._energy_min = self.getProperty('EnergyMin').value
        self._energy_max = self.getProperty('EnergyMax').value
        self._output_ws = self.getPropertyValue('OutputWorkspace')

    def _CheckHistZero(self, ws):
        """
        Retrieves basic info on a workspace

        Checks the workspace is not empty, then returns the number of histogram and
        the number of X-points, which is the number of bin boundaries minus one

        Args:
          @param ws  2D workspace

        Returns:
          @return num_hist - number of histograms in the workspace
          @return ntc - number of X-points in the first histogram, which is the number of bin
           boundaries minus one. It is assumed all histograms have the same
           number of X-points.

        Raises:
          @exception ValueError - Workspace has no histograms
        """
        num_hist = mtd[ws].getNumberHistograms()  # no. of hist/groups in WS
        if num_hist == 0:
            raise ValueError('Workspace %s has NO histograms' % ws)
        x_in = mtd[ws].readX(0)
        ntc = len(x_in) - 1  # no. points from length of x array
        if ntc == 0:
            raise ValueError('Workspace %s has NO points' % ws)
        return num_hist, ntc

    def _CheckElimits(self, erange, ws):
        import math
        x_data = np.asarray(mtd[ws].readX(0))
        len_x = len(x_data) - 1

        if math.fabs(erange[0]) < 1e-5:
            raise ValueError('Elimits - input emin (%f) is Zero' % (erange[0]))
        if erange[0] < x_data[0]:
            raise ValueError('Elimits - input emin (%f) < data emin (%f)' % (erange[0], x_data[0]))
        if math.fabs(erange[1]) < 1e-5:
            raise ValueError('Elimits - input emax (%f) is Zero' % (erange[1]))
        if erange[1] > x_data[len_x]:
            raise ValueError('Elimits - input emax (%f) > data emax (%f)' % (erange[1], x_data[len_x]))
        if erange[1] < erange[0]:
            raise ValueError('Elimits - input emax (%f) < emin (%f)' % (erange[1], erange[0]))


# Register algorithm with Mantid
AlgorithmFactory.subscribe(SofQWMoments)
